#include "content-streamer.h"
#include "led-matrix.h"
#include "pixel-mapper.h"
#include <Magick++.h>
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cstdint>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <magick/image.h>
#include <map>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
// #include <boost/chrono.hpp>
// #include <boost/thread/thread.hpp>
#include <wiringPi.h>

// #include "header.h"

typedef int64_t tmillis_t;
static const tmillis_t distant_future = (1LL << 40); // that is a while.
static tmillis_t GetTimeInMillis() {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}
int TLstate = 0;
using rgb_matrix::Canvas;
using rgb_matrix::FrameCanvas;
using rgb_matrix::GPIO;
using rgb_matrix::RGBMatrix;
using rgb_matrix::StreamReader;
struct FileInfo {
  rgb_matrix::StreamIO *content_stream;
  std::vector<bool> UnicolLightL[3]; //[3]={false,false,false}
  std::vector<bool> UnicolLightR[3]; //[3]={false,false,false}
};

std::vector<FileInfo *> file_imgs;
RGBMatrix::Options matrix_options;
rgb_matrix::RuntimeOptions runtime_opt;
int vsync_multiple = 1;
bool do_center = false;
// int wait_ms = 67;
int wait_ms = 125;
FrameCanvas *offscreen_canvas;
FileInfo *file_info = NULL;
void loadScenario(std::vector<FileInfo *> &file_imgs, std::vector<int> &fName);

RGBMatrix *matrix;
//------------------------SERVER-------------------------------------------

#define UniconLight0 8
#define UniconLight1 9
#define UniconLight2 27
int ButtonState[2];
#define Button 25
#define CM_ON 1
#define CM_OFF 0
int64_t ButtonTime;
int64_t ButtonTimeHold;
bool breakAnimationLoop = false;
int currentScenarioNum = 0;
// void set_channel(int gpioPin, int mode) { digitalWrite(gpioPin, mode); };
void set_channel(int channel, int mode) { digitalWrite(Button + channel, mode); };

// constexpr size_t length(T(&)[N]) { return N; }
// using namespace std;
// #define IPaddr "127.0.0.1"
#define IPaddr "192.168.88.100"
#define PORT "1235"      // the port client will be connecting to
#define MAXDATASIZE 1024 // max number of bytes we can get at once

int sockfd, numbytes, ind = 0;
uint16_t size = 0;
std::array<uint8_t, MAXDATASIZE> buf;
std::array<uint8_t, MAXDATASIZE> buffer;

class UserTCPprotocol {
  // UserTCPprotocol();
  // ~UserTCPprotocol();
public:
  enum class Type : uint8_t { COMMUNICATION, STATE, SETTINGS, DATA };
  enum class Communication : uint8_t { WHO, ACK, RCV };
  enum class State : uint8_t { STOP, START, RESET };
  enum class Data : uint8_t { UNICON, BUTTON, SCENARIO };
  enum class Unicon : uint8_t { LED0, LED1, LED2 };
  enum class Scenario : uint8_t { ALLCOMBINATIONS, NEXTCOMBO };
  enum class ClientName : uint8_t { TL12, TL34, TL56, TL78 };
};
int clientName = static_cast<int>(UserTCPprotocol::ClientName::TL12);

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }
  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
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
template <class T, size_t N> void sentSocData(int sockfd, std::array<T, N> buf) {
  int bytes_sent;
  if ((bytes_sent = send(sockfd, buf.data(), buf.size(), 0)) == -1) {
    perror("send");
  } else {
    if (bytes_sent < buf.size()) {
      printf("sent %d byte out of %d\n", bytes_sent, (int)buf.size());
    } else {
      printf("all byte sent %d\n", bytes_sent);
    }
  }
}
void read_keys(int gpioPin) {
  ButtonState[0] = digitalRead(gpioPin);
  if (ButtonState[0] != ButtonState[1]) {
    ButtonState[1] = ButtonState[0];

    const tmillis_t start_ButtonTime = GetTimeInMillis();
    if (start_ButtonTime - ButtonTime > 1000) {
      ButtonTime = start_ButtonTime;
      if (!ButtonState[0]) {
        // std::array<uint8_t, 5> ar = {0, 3, static_cast<int>(UserTCPprotocol::Type::DATA), static_cast<int>(UserTCPprotocol::Data::BUTTON), static_cast<int>(UserTCPprotocol::Data::BUTTON)};
        std::array<uint8_t, 5> ar = {0, 3, static_cast<int>(UserTCPprotocol::Type::DATA), static_cast<int>(UserTCPprotocol::Data::BUTTON), 1};
        sentSocData(sockfd, ar);
      }
    }
  } else {
    if (!ButtonState[0]) {
      const tmillis_t start_ButtonTime = GetTimeInMillis();
      if (start_ButtonTime - ButtonTimeHold > 5000) {
        ButtonTimeHold = start_ButtonTime;
        std::array<uint8_t, 5> ar = {0, 3, static_cast<int>(UserTCPprotocol::Type::DATA), static_cast<int>(UserTCPprotocol::Data::BUTTON), 2};
        sentSocData(sockfd, ar);
      }
    } else {
      ButtonTimeHold = GetTimeInMillis();
    }
  }
}
// void set_channel(int gpioPin, int mode) { digitalWrite(gpioPin, mode); };
void DoCmd(int sockfd, uint8_t data[]) {
  // 1. COMMUNICATION
  if (data[0] == static_cast<int>(UserTCPprotocol::Type::COMMUNICATION)) {
    if (data[1] == static_cast<int>(UserTCPprotocol::Communication::WHO)) {
      std::array<uint8_t, 4> ar = {0, 2, 0, clientName};
      sentSocData(sockfd, ar);
    }
    // 2.
    else if (data[1] == static_cast<int>(UserTCPprotocol::Communication::ACK)) {
      // std::array<uint8_t, 4> ar = {0, 2, 1, 1};
      // sentSocData(sockfd, ar);
    }
  }
  // 2. STATE
  else if (data[0] == static_cast<int>(UserTCPprotocol::Type::STATE)) {
    printf("UserTCPprotocol STATE ");
    if (data[1] == static_cast<int>(UserTCPprotocol::State::STOP)) {
      printf("OFF\n");
      TLstate = 0;
    } else if (data[1] == static_cast<int>(UserTCPprotocol::State::START)) {
      printf("ON\n");
      TLstate = 1;
    }
  }
  // 3. SETTINGS
  else if (data[0] == static_cast<int>(UserTCPprotocol::Type::SETTINGS)) {
    printf("UserTCPprotocol SETTINGS ");
  }
  // 4. DATA
  else if (data[0] == static_cast<int>(UserTCPprotocol::Type::DATA)) {
    printf("UserTCPprotocol DATA ");
    // 4.1 UNICON
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
    }
    // 4.2  SCENARIO
    else if (data[1] == static_cast<int>(UserTCPprotocol::Data::SCENARIO)) {
      // NEWCOMBINATIONS, SECRETCOMBO, NEXTCOMBO
      if (data[2] == static_cast<int>(UserTCPprotocol::Scenario::ALLCOMBINATIONS)) {
        printf("UserTCPprotocol NEWCOMBINATIONS \n");
        std::vector<int> filename;
        for (int i = 0; i < 10; i++) {
          filename.push_back(data[4 + i]);
        }
        breakAnimationLoop = true;
        loadScenario(file_imgs, filename);
      } else if (data[2] == static_cast<int>(UserTCPprotocol::Scenario::NEXTCOMBO)) {
        if (data[3] < file_imgs.size()) {
          breakAnimationLoop = true;
          currentScenarioNum = data[3];
        }
      }
    }
  }
}
volatile bool interrupt_received = false;
void TCPread() {
  // do {
  // handle data from a server
  if ((numbytes = recv(sockfd, &buf, MAXDATASIZE - 1, 0)) <= 0) {
    if (numbytes == 0) {
      printf("Connection closed\n");
      close(sockfd);
    } else {
      if (errno != EWOULDBLOCK) {
        perror("recv failed");
        close(sockfd);
      }
    }
    // reconect();
  } else {
    // we got some data from a client
    if (numbytes > 0) {
      printf("tcp new array length: %d\n ", numbytes);
      memcpy(buffer.data() + sizeof(uint8_t) * ind, buf.data(),
             sizeof(uint8_t) * buf.size()); // void * memcpy ( void * destination, const
                                            // void * source, size_t num );
      ind += numbytes;

      for (int i = 0; i < numbytes; i++) {
        printf("%d ", buffer[i]);
      }
      printf("\nind: %d\n", ind);

      while ((size == 0 && ind >= sizeof(size)) || (size > 0 && ind >= size)) // While can process data, process it
      {
        if (size == 0 && ind >= sizeof(size)) // if size of data has received completely,
                                              // then store it on our global variable
        {
          size = (uint16_t)(buffer[0] << 8);
          size |= buffer[1];
          printf("tcp msg size: %d\n", size);
        }
        if (size > 0 && ind >= size + sizeof(size)) // If data has received completely,
                                                    // then emit our SIGNAL with the data
        {
          uint8_t data[1024 * 3];
          memcpy(data, buffer.data() + sizeof(uint8_t) * sizeof(size),
                 sizeof(uint8_t) * size); // coppy data from temp buffer

          for (int i = 0; i < size; i++) {
            printf("%d ", data[i]);
          }
          printf("\n");

          // memmove(buffer.data(), buffer.data() + size + sizeof(size), sizeof(uint8_t) * (ind - (size + sizeof(size)))); // move all data from temp buffer to 0 after Size void * memmove ( void * destination, const void * source, size_t num );
          memmove(buffer.data(), buffer.data() + size + sizeof(size), ind - (size + sizeof(size)));
          printf("memmove from: %d  num: %d\n", size + sizeof(size), ind - (size + sizeof(size)));
          printf("\n");
          ind -= size + 2;
          memset(buffer.data() + sizeof(uint8_t) * ind, 0,
                 sizeof(uint8_t) * (buf.size() - ind)); // void * memset ( void * ptr, int

          // value, size_t num );
          size = 0;
          DoCmd(sockfd, data);
        }
      }
      //-------------------
      // while ((size == 0 && buffer.size() >= 2) || (size > 0 && buffer.size() >= size)) // While can process data, process it
      // {
      //   if (size == 0 && buffer.size() >= 2) // if size of data has received completely, then store it on our global variable
      //   {
      //     size = ArrayToInt(buffer.mid(0, 2));
      //     buffer.erase(vec.begin() + 1, vec.begin() + 3);
      //   }
      //   if (size > 0 && buffer.size() >= size) // If data has received completely, then emit our SIGNAL with the data
      //   {
      //     QByteArray data = buffer.mid(0, size);
      //     buffer.remove(0, size);
      //     size = 0;
      //     // qDebug() <<"2. GET MSG: "<<data;

      //     quint8 array[data.size()];
      //     memcpy(array, data.data(), data.count());
      //     QString strData;
      //     for (int i = 0; i < data.count(); i++) {
      //       strData.append(QString::number((quint8)data.at(i)).append(" "));
      //     }
      //     DoCmd(sockfd, data);
      //   }
      // }
    }
  }
  //} while (do_forever && !interrupt_received);
}
//------------------------ANIMATION----------------------------------------

static void InterruptHandler(int signo) { interrupt_received = true; }
static void SleepMillis(tmillis_t milli_seconds) {
  if (milli_seconds <= 0)
    return;
  struct timespec ts;
  ts.tv_sec = milli_seconds / 1000;
  ts.tv_nsec = (milli_seconds % 1000) * 1000000;
  nanosleep(&ts, NULL);
}
static void StoreInStream(const Magick::Image &img, int delay_time_us, bool do_center, rgb_matrix::FrameCanvas *scratch, rgb_matrix::StreamWriter *output) {
  scratch->Clear();
  const int x_offset = do_center ? (scratch->width() - img.columns()) / 2 : 0;
  const int y_offset = do_center ? (scratch->height() - img.rows()) / 2 : 0;
  for (size_t y = 0; y < img.rows(); ++y) {
    for (size_t x = 0; x < img.columns(); ++x) {
      const Magick::Color &c = img.pixelColor(x, y);
      if (c.alphaQuantum() < 256) {
        scratch->SetPixel(x + x_offset, y + y_offset, ScaleQuantumToChar(c.redQuantum()), ScaleQuantumToChar(c.greenQuantum()), ScaleQuantumToChar(c.blueQuantum()));
      }
    }
  }
  output->Stream(*scratch, delay_time_us);
}
void DisplayAnimation(const FileInfo *file, RGBMatrix *matrix, FrameCanvas *offscreen_canvas, int vsync_multiple) {
  rgb_matrix::StreamReader reader(file->content_stream);
  while (!interrupt_received && !breakAnimationLoop) {
    TCPread();
    read_keys(Button);
    if (TLstate == 1) {
      uint32_t delay_us = 0;
      int seqInd = 0;
      while (!interrupt_received && reader.GetNext(offscreen_canvas, &delay_us) && !breakAnimationLoop) {
        // printf("unicol Light: UP:%i, MID:%i, BOT:%i\n", file->UnicolLight[0].at(seqInd), file->UnicolLight[1].at(seqInd), file->UnicolLight[2].at(seqInd));
        digitalWrite(UniconLight0, file->UnicolLightL[0].at(seqInd));
        digitalWrite(UniconLight1, file->UnicolLightL[1].at(seqInd));
        digitalWrite(UniconLight2, file->UnicolLightL[2].at(seqInd));
        seqInd++;
        const tmillis_t anim_delay_ms = delay_us / 1000;
        const tmillis_t start_wait_ms = GetTimeInMillis();
        offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas, vsync_multiple);
        const tmillis_t time_already_spent = GetTimeInMillis() - start_wait_ms;
        SleepMillis(anim_delay_ms - time_already_spent);
        TCPread();
        read_keys(Button);
      }
      reader.Rewind();
    } else if (TLstate == 0) {
      file_imgs.clear();
      matrix->Clear();
    }
  }
}
void loadScenario(std::vector<FileInfo *> &file_imgs, std::vector<int> &fName) {
  // matrix = CreateMatrixFromOptions(matrix_options, runtime_opt);
  // if (matrix == NULL) {
  //   return 1;
  // }
  matrix->Clear();
  offscreen_canvas = matrix->CreateFrameCanvas();

  file_info = new FileInfo();
  file_info->content_stream = new rgb_matrix::MemStreamIO();

  // file_imgs.clear();
  rgb_matrix::StreamIO *stream_io = NULL;

  Magick::Image emptyImg("32x32", "black");
  std::vector<Magick::Image> image_sequence[10];
  std::vector<Magick::Image> frames[10];

  for (int imgarg = 0; imgarg < 10; imgarg++) {
    printf("loadScenario: %i, %i\n", imgarg, fName[imgarg]);
    std::string s;

    if (fName[imgarg] == 0) {
      image_sequence[imgarg].push_back(emptyImg);
    } else {
      s = std::to_string(fName[imgarg]);

      readImages(&frames[imgarg], s.append(".gif"));
      if (frames[imgarg].size() > 1) {
        Magick::coalesceImages(&image_sequence[imgarg], frames[imgarg].begin(), frames[imgarg].end());
      } else {
        image_sequence[imgarg].push_back(frames[imgarg][0]);
      }
    }
  }

  std::vector<Magick::Image> calage;
  auto maxValue = std::max({image_sequence[0].size(), image_sequence[1].size(), image_sequence[2].size(), image_sequence[3].size(), image_sequence[4].size(), image_sequence[5].size(), image_sequence[6].size(), image_sequence[7].size(),
                            image_sequence[8].size(), image_sequence[9].size()});
  for (size_t i = 0; i < maxValue; ++i) {
    Magick::Image appendedL;
    std::vector<Magick::Image> stackL;
    stackL.push_back(i < image_sequence[0].size() ? image_sequence[0][i] : emptyImg);
    stackL.push_back(i < image_sequence[1].size() ? image_sequence[1][i] : emptyImg);
    stackL.push_back(i < image_sequence[2].size() ? image_sequence[2][i] : emptyImg);
    stackL.push_back(i < image_sequence[3].size() ? image_sequence[3][i] : emptyImg);
    stackL.push_back(i < image_sequence[4].size() ? image_sequence[4][i] : emptyImg);
    Magick::appendImages(&appendedL, stackL.begin(), stackL.end());

    Magick::Image appendedR;
    std::vector<Magick::Image> stackR;
    stackR.push_back(i < image_sequence[5].size() ? image_sequence[5][i] : emptyImg);
    stackR.push_back(i < image_sequence[6].size() ? image_sequence[6][i] : emptyImg);
    stackR.push_back(i < image_sequence[7].size() ? image_sequence[7][i] : emptyImg);
    stackR.push_back(i < image_sequence[8].size() ? image_sequence[8][i] : emptyImg);
    stackR.push_back(i < image_sequence[9].size() ? image_sequence[9][i] : emptyImg);
    Magick::appendImages(&appendedR, stackR.begin(), stackR.end());

    std::vector<Magick::Image>::iterator it;
    for (auto it = stackL.begin(); std::distance(stackL.begin(), it) < 3; ++it) {
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
      file_info->UnicolLightL[std::distance(stackL.begin(), it)].push_back(!isEmptry);
    }

    // std::vector<Magick::Image>::iterator it;
    for (auto it = stackR.begin(); std::distance(stackR.begin(), it) < 3; ++it) {
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
      file_info->UnicolLightR[std::distance(stackR.begin(), it)].push_back(!isEmptry);
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
    const Magick::Image &img = calage[i];
    int64_t delay_time_us = wait_ms * 1000;
    StoreInStream(img, delay_time_us, do_center, offscreen_canvas, &out);
  }

  if (file_info) {
    file_imgs.push_back(file_info);
  }
  printf("file_imgs size: %i\n", file_imgs.size());
}
void setCurrentScenario(int num) {}
//
//------------------------MAIN----------------------------------------
//
int main(int argc, char *argv[]) {
  wiringPiSetup();
  pinMode(UniconLight0, OUTPUT);
  pinMode(UniconLight1, OUTPUT);
  pinMode(UniconLight2, OUTPUT);

  digitalWrite(UniconLight0, CM_OFF);
  digitalWrite(UniconLight1, CM_OFF);
  digitalWrite(UniconLight2, CM_OFF);

  pinMode(Button, INPUT);

  // for (int i = 0; i < 3; ++i) {
  //   digitalWrite(UniconLight0, CM_ON);
  //   SleepMillis(500);
  //   digitalWrite(UniconLight1, CM_ON);
  //   SleepMillis(500);
  //   digitalWrite(UniconLight2, CM_ON);
  //   SleepMillis(500);
  //   digitalWrite(UniconLight0, CM_OFF);
  //   digitalWrite(UniconLight1, CM_OFF);
  //   digitalWrite(UniconLight2, CM_OFF);
  //   SleepMillis(500);
  // }

  struct addrinfo hints, *servinfo, *p;
  int rv;
  char s[INET6_ADDRSTRLEN];

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if ((rv = getaddrinfo(IPaddr, PORT, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    quit(1);
  }

  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("client: socket");
      continue;
    }
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("client: connect");
      continue;
    }
    break;
  }

  if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK) < 0) {
    perror("client: socket non-blocking mode");
  }
  if (p == NULL) {
    fprintf(stderr, "client: failed to connect\n");
  }

  inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
  printf("client: connecting to %s\n", s);
  freeaddrinfo(servinfo);
  //
  //------------------------ANIMATION----------------------------------------
  //
  Magick::InitializeMagick(*argv);
  matrix_options.rows = 32;
  matrix_options.cols = 32;
  matrix_options.chain_length = 5;
  matrix_options.parallel = 2;
  matrix_options.scan_mode = 0;
  matrix_options.multiplexing = 2;
  matrix_options.led_rgb_sequence = "BGR";
  matrix_options.pwm_bits = 7;
  matrix_options.brightness = 70;
  matrix = CreateMatrixFromOptions(matrix_options, runtime_opt);
  if (matrix == NULL) {
    return 1;
  }
  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);
  signal(SIGTSTP, InterruptHandler);

  do {
    read_keys(Button);
    TCPread();
    if (TLstate) {
      breakAnimationLoop = false;
      DisplayAnimation(file_imgs[currentScenarioNum], matrix, offscreen_canvas, vsync_multiple);
    }
  } while (!interrupt_received);

  if (interrupt_received) {
    fprintf(stderr, "Caught signal. Exiting.\n");
  }
  try {
    close(sockfd);
    matrix->Clear();
    delete matrix;
  } catch (std::exception &e) {
    std::string err_msg = e.what();
    fprintf(stderr, "catch err: %s\n", err_msg);
    return false;
  }

  fprintf(stderr, "quit\n");
  quit(0);
}
