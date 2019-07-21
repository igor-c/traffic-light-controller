#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <Magick++.h>
#include <magick/image.h>
#include <wiringPi.h>

#include "content-streamer.h"
#include "led-matrix.h"
#include "pixel-mapper.h"

#define IPaddr "192.168.88.100"
#define PORT "1235"       // the port client will be connecting to
#define MAXDATASIZE 1024  // max number of bytes we can get at once

constexpr bool kDoCenter = false;

using tmillis_t = int64_t;

struct FileInfo {
  rgb_matrix::StreamIO* content_stream;
  std::vector<bool> UnicolLightL[3];  //[3]={false,false,false}
  std::vector<bool> UnicolLightR[3];  //[3]={false,false,false}
};

class UserTCPprotocol {
 public:
  enum class Type : uint8_t { COMMUNICATION, STATE, SETTINGS, DATA };
  enum class Communication : uint8_t { WHO, ACK, RCV };
  enum class State : uint8_t { STOP, START, RESET };
  enum class Settings : uint8_t { MPANEL };
  enum class MPanel : uint8_t {
    SCANMODE,
    MULTIPLEXING,
    PWMBIT,
    BRIGHTNESS,
    GPIOSLOWDOWN
  };
  enum class Data : uint8_t { UNICON, BUTTON, SCENARIO };
  enum class Unicon : uint8_t { LED0, LED1, LED2 };
  enum class Scenario : uint8_t { ALLCOMBINATIONS, NEXTCOMBO };
  enum class ClientName : uint8_t { TL12, TL34, TL56, TL78 };
};

#define UniconLight0 8
#define UniconLight1 9
#define UniconLight2 27
#define CM_ON 1
#define CM_OFF 0

static int TLstate = 0;
static std::vector<FileInfo*> file_imgs;
static int vsync_multiple = 1;
static int wait_ms = 125;
static rgb_matrix::FrameCanvas* offscreen_canvas;
static rgb_matrix::RGBMatrix* matrix;
static bool breakAnimationLoop = false;
static int currentScenarioNum = 0;
static int socket_fd = -1;
static bool interrupt_received = false;
static int clientName = static_cast<int>(UserTCPprotocol::ClientName::TL12);

void LoadScenario(std::vector<FileInfo*>& file_imgs, std::vector<int>& fName);

//------------------------SERVER-------------------------------------------

static tmillis_t GetTimeInMillis() {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

static void SleepMillis(tmillis_t milli_seconds) {
  if (milli_seconds <= 0)
    return;
  struct timespec ts;
  ts.tv_sec = milli_seconds / 1000;
  ts.tv_nsec = (milli_seconds % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

void* get_in_addr(struct sockaddr* sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }
  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void quit(int val) {
  while (1) {
    std::cout << "Press 'q' or 'Q'  and ENTER to quit\n";
    char c;
    std::cin >> c;
    if (c == 'Q' || c == 'q') {
      if (val == 0) {
        exit(EXIT_SUCCESS);
      } else if (val == 1) {
        exit(EXIT_FAILURE);
      }
    }
  }
}

static void DisconnectFromServer() {
  if (socket_fd != -1) {
    close(socket_fd);
    socket_fd = -1;
  }
}

static void TrySendToServer(const uint8_t* new_data, size_t new_data_len) {
  static std::vector<std::vector<uint8_t>> buffered;

  if (new_data_len)
    buffered.emplace_back(new_data, new_data + new_data_len);

  while (!buffered.empty()) {
    std::vector<uint8_t>& buffer = buffered.front();
    ssize_t bytes_sent = send(socket_fd, buffer.data(), buffer.size(), 0);

    if (bytes_sent == -1) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
      perror("send() failed");
      DisconnectFromServer();
      return;
    }

    buffer.erase(buffer.begin(), buffer.begin() + bytes_sent);
    if (buffer.empty())
      buffered.erase(buffered.begin());
  }
}

void DoCmd(uint8_t data[]) {
  if (data[0] == static_cast<int>(UserTCPprotocol::Type::COMMUNICATION)) {
    if (data[1] == static_cast<int>(UserTCPprotocol::Communication::WHO)) {
      uint8_t ar[] = {0, 2, 0, clientName};
      TrySendToServer(ar, sizeof(ar));
    } else if (data[1] ==
               static_cast<int>(UserTCPprotocol::Communication::ACK)) {
      // std::array<uint8_t, 4> ar = {0, 2, 1, 1};
      // TrySendToServer(ar);
    }
    return;
  }

  if (data[0] == static_cast<int>(UserTCPprotocol::Type::STATE)) {
    printf("UserTCPprotocol STATE ");
    if (data[1] == static_cast<int>(UserTCPprotocol::State::STOP)) {
      printf("OFF\n");
      TLstate = 0;
    } else if (data[1] == static_cast<int>(UserTCPprotocol::State::START)) {
      printf("ON\n");
      TLstate = 1;
    }
    return;
  }

  if (data[0] == static_cast<int>(UserTCPprotocol::Type::SETTINGS)) {
    printf("UserTCPprotocol SETTINGS");
    return;
  }

  if (data[0] != static_cast<int>(UserTCPprotocol::Type::DATA))
    return;

  printf("UserTCPprotocol DATA");
  if (data[1] == static_cast<int>(UserTCPprotocol::Data::UNICON)) {
    if (data[2] == static_cast<int>(UserTCPprotocol::Unicon::LED0)) {
      if (data[3] == 0) {
        printf("ON\n");
        digitalWrite(UniconLight0, CM_OFF);
      } else if (data[3] == 2) {
        printf("OFF\n");
        digitalWrite(UniconLight0, CM_ON);
      }
    } else if (data[2] == static_cast<int>(UserTCPprotocol::Unicon::LED1)) {
      if (data[3] == 0) {
        printf("ON\n");
        digitalWrite(UniconLight1, CM_OFF);
      } else if (data[3] == 2) {
        printf("OFF\n");
        digitalWrite(UniconLight1, CM_ON);
      }
    } else if (data[2] == static_cast<int>(UserTCPprotocol::Unicon::LED2)) {
      if (data[3] == 0) {
        printf("ON\n");
        digitalWrite(UniconLight2, CM_OFF);
      } else if (data[3] == 2) {
        printf("OFF\n");
        digitalWrite(UniconLight2, CM_ON);
      }
    }
    return;
  }

  if (data[1] == static_cast<int>(UserTCPprotocol::Data::SCENARIO)) {
    // NEWCOMBINATIONS, SECRETCOMBO, NEXTCOMBO
    // ALLCOMBINATIONS(0), NEXTCOMBO (1)
    if (data[2] ==
        static_cast<int>(UserTCPprotocol::Scenario::ALLCOMBINATIONS)) {
      printf("UserTCPprotocol NEWCOMBINATIONS \n");
      std::vector<int> filename;
      for (int i = 0; i < 10; i++) {
        filename.push_back(data[4 + i]);
      }
      breakAnimationLoop = true;
      LoadScenario(file_imgs, filename);
    } else if (data[2] ==
               static_cast<int>(UserTCPprotocol::Scenario::NEXTCOMBO)) {
      if (data[3] < file_imgs.size()) {
        breakAnimationLoop = true;
        currentScenarioNum = data[3];
      }
    }
  }
}

std::vector<uint8_t> ReadSocketCommand() {
  static uint8_t tmp[1024];
  static std::vector<uint8_t> cached;

  std::vector<uint8_t> result;
  if (socket_fd == -1)
    return std::vector<uint8_t>();

  while (true) {
    // Try to send the data when we can, as we have no proper AsyncIO handling.
    TrySendToServer(nullptr, 0);
    if (socket_fd == -1)
      return std::vector<uint8_t>();

    ssize_t num_bytes = recv(socket_fd, tmp, sizeof(tmp), 0);
    if (num_bytes == -1) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      perror("recv() failed");
      DisconnectFromServer();
      break;
    }

    size_t old_cached_size = cached.size();
    cached.resize(old_cached_size + num_bytes);
    std::memcpy(cached.data() + old_cached_size, tmp, num_bytes);
  }

  while (true) {
    if (cached.size() < 2)
      return std::vector<uint8_t>();

    uint16_t packet_size = (uint16_t) (cached[0] << 8) | cached[1];
    if (packet_size == 0) {
      // Continue here to make sure empty packet always means no more data.
      cached.erase(cached.begin(), cached.begin() + 2);
      continue;
    }

    if (cached.size() < (2 + packet_size))
      return std::vector<uint8_t>();

    result.resize(packet_size);
    std::memcpy(result.data(), cached.data() + 2, packet_size);
    cached.erase(cached.begin(), cached.begin() + 2 + packet_size);
    return result;
  }
}

void ReadSocketAndExecuteCommand() {
  while (true) {
    std::vector<uint8_t> command = ReadSocketCommand();
    if (command.empty())
      break;

    DoCmd(command.data());
  }
}

static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static void StoreInStream(const Magick::Image& img,
                          int delay_time_us,
                          rgb_matrix::FrameCanvas* scratch,
                          rgb_matrix::StreamWriter* output) {
  scratch->Clear();
  const int x_offset = kDoCenter ? (scratch->width() - img.columns()) / 2 : 0;
  const int y_offset = kDoCenter ? (scratch->height() - img.rows()) / 2 : 0;
  for (size_t y = 0; y < img.rows(); ++y) {
    for (size_t x = 0; x < img.columns(); ++x) {
      const Magick::Color& c = img.pixelColor(x, y);
      if (c.alphaQuantum() < 256) {
        scratch->SetPixel(x + x_offset, y + y_offset,
                          ScaleQuantumToChar(c.redQuantum()),
                          ScaleQuantumToChar(c.greenQuantum()),
                          ScaleQuantumToChar(c.blueQuantum()));
      }
    }
  }
  output->Stream(*scratch, delay_time_us);
}

void DisplayAnimation(const FileInfo* file,
                      rgb_matrix::RGBMatrix* matrix,
                      rgb_matrix::FrameCanvas* offscreen_canvas,
                      int vsync_multiple) {
  rgb_matrix::StreamReader reader(file->content_stream);
  while (!interrupt_received && !breakAnimationLoop) {
    ReadSocketAndExecuteCommand();
    if (TLstate == 1) {
      uint32_t delay_us = 0;
      int seqInd = 0;
      while (!interrupt_received &&
             reader.GetNext(offscreen_canvas, &delay_us) &&
             !breakAnimationLoop) {
        // printf("unicol Light: UP:%i, MID:%i, BOT:%i\n",
        // file->UnicolLight[0].at(seqInd), file->UnicolLight[1].at(seqInd),
        // file->UnicolLight[2].at(seqInd));
        digitalWrite(UniconLight0, file->UnicolLightL[0].at(seqInd));
        digitalWrite(UniconLight1, file->UnicolLightL[1].at(seqInd));
        digitalWrite(UniconLight2, file->UnicolLightL[2].at(seqInd));
        seqInd++;
        const tmillis_t anim_delay_ms = delay_us / 1000;
        const tmillis_t start_wait_ms = GetTimeInMillis();
        offscreen_canvas =
            matrix->SwapOnVSync(offscreen_canvas, vsync_multiple);
        const tmillis_t time_already_spent = GetTimeInMillis() - start_wait_ms;
        SleepMillis(anim_delay_ms - time_already_spent);
        ReadSocketAndExecuteCommand();
      }
      reader.Rewind();
    } else if (TLstate == 0) {
      file_imgs.clear();
      matrix->Clear();
    }
  }
}

void LoadScenario(std::vector<FileInfo*>& file_imgs, std::vector<int>& fName) {
  matrix->Clear();
  offscreen_canvas = matrix->CreateFrameCanvas();

  FileInfo* file_info = new FileInfo();
  file_info->content_stream = new rgb_matrix::MemStreamIO();

  // file_imgs.clear();
  Magick::Image emptyImg("32x32", "black");
  std::vector<Magick::Image> image_sequence[10];
  std::vector<Magick::Image> frames[10];

  for (int imgarg = 0; imgarg < 10; imgarg++) {
    printf("LoadScenario: %i, %i\n", imgarg, fName[imgarg]);

    // int leading = 4; //6 at max
    // //printf(std::to_string(imgarg*0.000001).substr(8-leading));
    // std::array<std::string, 18> ar = {"LoadScenario:
    // "+std::to_string(imgarg*0.000001).substr(8-leading)}; TrySendToServer(sockfd,
    // ar);

    std::string s;
    std::string s2("gif/");

    if (fName[imgarg] == 0) {
      image_sequence[imgarg].push_back(emptyImg);
    } else {
      s = std::to_string(fName[imgarg]);
      s.insert(0, s2);
      readImages(&frames[imgarg], s.append(".gif"));
      printf("readImages path: %s\n", s.c_str());
      if (frames[imgarg].size() > 1) {
        Magick::coalesceImages(&image_sequence[imgarg], frames[imgarg].begin(),
                               frames[imgarg].end());
      } else {
        image_sequence[imgarg].push_back(frames[imgarg][0]);
      }
    }
  }

  std::vector<Magick::Image> calage;
  auto maxValue =
      std::max({image_sequence[0].size(), image_sequence[1].size(),
                image_sequence[2].size(), image_sequence[3].size(),
                image_sequence[4].size(), image_sequence[5].size(),
                image_sequence[6].size(), image_sequence[7].size(),
                image_sequence[8].size(), image_sequence[9].size()});
  for (size_t i = 0; i < maxValue; ++i) {
    Magick::Image appendedL;
    std::vector<Magick::Image> stackL;
    stackL.push_back(i < image_sequence[0].size() ? image_sequence[0][i]
                                                  : emptyImg);
    stackL.push_back(i < image_sequence[1].size() ? image_sequence[1][i]
                                                  : emptyImg);
    stackL.push_back(i < image_sequence[2].size() ? image_sequence[2][i]
                                                  : emptyImg);
    stackL.push_back(i < image_sequence[3].size() ? image_sequence[3][i]
                                                  : emptyImg);
    stackL.push_back(i < image_sequence[4].size() ? image_sequence[4][i]
                                                  : emptyImg);
    Magick::appendImages(&appendedL, stackL.begin(), stackL.end());

    Magick::Image appendedR;
    std::vector<Magick::Image> stackR;
    stackR.push_back(i < image_sequence[5].size() ? image_sequence[5][i]
                                                  : emptyImg);
    stackR.push_back(i < image_sequence[6].size() ? image_sequence[6][i]
                                                  : emptyImg);
    stackR.push_back(i < image_sequence[7].size() ? image_sequence[7][i]
                                                  : emptyImg);
    stackR.push_back(i < image_sequence[8].size() ? image_sequence[8][i]
                                                  : emptyImg);
    stackR.push_back(i < image_sequence[9].size() ? image_sequence[9][i]
                                                  : emptyImg);
    Magick::appendImages(&appendedR, stackR.begin(), stackR.end());

    std::vector<Magick::Image>::iterator it;
    for (auto it = stackL.begin(); std::distance(stackL.begin(), it) < 3;
         ++it) {
      int imgWidth = it->columns();
      int imgHeight = it->rows();
      bool isEmptry = true;
      for (int row = 0; row <= imgHeight; row++) {
        for (int column = 0; column <= imgWidth; column++) {
          Magick::ColorRGB px = it->pixelColor(column, row);
          if (px.red() > 0 || px.green() > 0 || px.blue() > 0) {
            isEmptry = false;
          }
        }
      }
      file_info->UnicolLightL[std::distance(stackL.begin(), it)].push_back(
          !isEmptry);
    }

    // std::vector<Magick::Image>::iterator it;
    for (auto it = stackR.begin(); std::distance(stackR.begin(), it) < 3;
         ++it) {
      int imgWidth = it->columns();
      int imgHeight = it->rows();
      bool isEmptry = true;
      for (int row = 0; row <= imgHeight; row++) {
        for (int column = 0; column <= imgWidth; column++) {
          Magick::ColorRGB px = it->pixelColor(column, row);
          if (px.red() > 0 || px.green() > 0 || px.blue() > 0) {
            isEmptry = false;
          }
        }
      }
      file_info->UnicolLightR[std::distance(stackR.begin(), it)].push_back(
          !isEmptry);
    }

    Magick::Image appended;
    std::vector<Magick::Image> stack;
    stack.push_back(appendedR);
    stack.push_back(appendedL);
    Magick::appendImages(&appended, stack.begin(), stack.end(), true);
    calage.push_back(appended);
  }

  rgb_matrix::StreamWriter out(file_info->content_stream);
  for (size_t i = 0; i < calage.size(); ++i) {
    const Magick::Image& img = calage[i];
    int64_t delay_time_us = wait_ms * 1000;
    StoreInStream(img, delay_time_us, offscreen_canvas, &out);
  }

  if (file_info) {
    file_imgs.push_back(file_info);
  }
  printf("file_imgs size: %i\n", file_imgs.size());
}

static void SetupUnicornPins() {
  wiringPiSetup();

  pinMode(UniconLight0, OUTPUT);
  pinMode(UniconLight1, OUTPUT);
  pinMode(UniconLight2, OUTPUT);

  digitalWrite(UniconLight0, CM_OFF);
  digitalWrite(UniconLight1, CM_OFF);
  digitalWrite(UniconLight2, CM_OFF);
}

static void ReconnectToServer() {
  DisconnectFromServer();

  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo* servinfo;
  int rv = getaddrinfo(IPaddr, PORT, &hints, &servinfo);
  if (rv != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return;
  }

  struct addrinfo* p;
  for (p = servinfo; p != NULL; p = p->ai_next) {
    char s[INET6_ADDRSTRLEN];
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr*)p->ai_addr), s,
              sizeof(s));
    printf("Connecting to %s\n", s);

    socket_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (socket_fd == -1) {
      perror("socket() failed");
      continue;
    }

    if (connect(socket_fd, p->ai_addr, p->ai_addrlen) == -1) {
      perror("connect() failed");
      DisconnectFromServer();
      continue;
    }

    if (fcntl(socket_fd, F_SETFL, fcntl(socket_fd, F_GETFL) | O_NONBLOCK) < 0) {
      perror("fcntl(O_NONBLOCK) failed");
      DisconnectFromServer();
      continue;
    }

    break;
  }

  if (p) {
    fprintf(stderr, "Connected to server\n");
  } else {
    fprintf(stderr, "Failed to connect to server\n");
  }

  freeaddrinfo(servinfo);
}

//
//------------------------MAIN----------------------------------------
//
int main(int argc, char* argv[]) {
  Magick::InitializeMagick(*argv);

  rgb_matrix::RGBMatrix::Options matrix_options;
  matrix_options.rows = 32;
  matrix_options.cols = 32;
  matrix_options.chain_length = 1;
  matrix_options.parallel = 1;
  matrix_options.scan_mode = 0;
  matrix_options.multiplexing = 2;
  matrix_options.led_rgb_sequence = "BGR";
  matrix_options.pwm_bits = 7;
  matrix_options.brightness = 70;

  rgb_matrix::RuntimeOptions runtime_opt;
  runtime_opt.gpio_slowdown = 2;

  int opt;
  while ((opt = getopt(argc, argv, "n:s:m:")) != -1) {
    switch (opt) {
      case 'n': {
        printf("indName: %s\n", optarg);
        int indName = atoi(optarg);
        printf("indName convert atoi: %i\n", indName);
        if (indName == 1)
          clientName = static_cast<int>(UserTCPprotocol::ClientName::TL12);
        else if (indName == 2)
          clientName = static_cast<int>(UserTCPprotocol::ClientName::TL34);
        else if (indName == 3)
          clientName = static_cast<int>(UserTCPprotocol::ClientName::TL56);
        else if (indName == 4)
          clientName = static_cast<int>(UserTCPprotocol::ClientName::TL78);
        break;
      }
      case 's': {
        printf("scan_mode: %s\n", optarg);
        int scan_mode = atoi(optarg);
        printf("scan_mode convert atoi: %i\n", scan_mode);
        matrix_options.scan_mode = scan_mode;
        break;
      }
      case 'm': {
        printf("multiplexing: %s\n", optarg);
        int multiplexing = atoi(optarg);
        printf("multiplexing convert atoi: %i\n", multiplexing);
        matrix_options.multiplexing = multiplexing;
        break;
      }
    }
  }

  SetupUnicornPins();

  ReconnectToServer();

  //
  //------------------------ANIMATION----------------------------------------
  //
  // --led-gpio-mapping=<name> : Name of GPIO mapping used. Default "regular"
  // --led-rows=<rows>         : Panel rows. Typically 8, 16, 32 or 64.
  // (Default: 32).
  // --led-cols=<cols>         : Panel columns. Typically 32 or 64. (Default:
  // 32).
  // --led-chain=<chained>     : Number of daisy-chained panels. (Default: 1).
  // --led-parallel=<parallel> : Parallel chains. range=1..3 (Default: 1).
  // --led-multiplexing=<0..6> : Mux type: 0=direct; 1=Stripe; 2=Checkered;
  // 3=Spiral; 4=ZStripe; 5=ZnMirrorZStripe; 6=coreman (Default: 0)
  // --led-pixel-mapper        : Semicolon-separated list of pixel-mappers to
  // arrange pixels.
  //                             Optional params after a colon e.g.
  //                             "U-mapper;Rotate:90" Available: "Rotate",
  //                             "U-mapper". Default: ""
  // --led-pwm-bits=<1..11>    : PWM bits (Default: 11).
  // --led-brightness=<percent>: Brightness in percent (Default: 100).
  // --led-scan-mode=<0..1>    : 0 = progressive; 1 = interlaced (Default: 0).
  // --led-row-addr-type=<0..2>: 0 = default; 1 = AB-addressed panels; 2 =
  // direct row select(Default: 0).
  // --led-show-refresh        : Show refresh rate.
  // --led-inverse             : Switch if your matrix has inverse colors on.
  // --led-rgb-sequence        : Switch if your matrix has led colors swapped
  // (Default: "RGB")
  // --led-pwm-lsb-nanoseconds : PWM Nanoseconds for LSB (Default: 130)
  // --led-no-hardware-pulse   : Don't use hardware pin-pulse generation.
  // --led-slowdown-gpio=<0..2>: Slowdown GPIO. Needed for faster Pis/slower
  // panels (Default: 1).
  // --led-daemon              : Make the process run in the background as
  // daemon.
  // --led-no-drop-privs       : Don't drop privileges from 'root' after
  // initializing the hardware.

  // matrix_options.rows = 32;
  // matrix_options.cols = 32;
  // matrix_options.chain_length = 5;
  // matrix_options.parallel = 2;
  // matrix_options.scan_mode = 0;
  // matrix_options.multiplexing = 2;
  // matrix_options.led_rgb_sequence = "BGR";
  // matrix_options.pwm_bits = 7;
  // matrix_options.brightness = 70;

  matrix = CreateMatrixFromOptions(matrix_options, runtime_opt);
  if (matrix == NULL) {
    return 1;
  }

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);
  signal(SIGTSTP, InterruptHandler);

  fprintf(stderr, "Entering processing loop\n");

  do {
    // SleepMillis(200);
    ReadSocketAndExecuteCommand();
    if (TLstate) {
      breakAnimationLoop = false;
      DisplayAnimation(file_imgs[currentScenarioNum], matrix, offscreen_canvas,
                       vsync_multiple);
    }
  } while (!interrupt_received);

  if (interrupt_received) {
    fprintf(stderr, "Caught signal. Exiting.\n");
  }
  try {
    DisconnectFromServer();
    matrix->Clear();
    delete matrix;
  } catch (std::exception& e) {
    std::string err_msg = e.what();
    fprintf(stderr, "catch err: %s\n", err_msg.c_str());
    return false;
  }

  fprintf(stderr, "quit\n");
  quit(0);
}
