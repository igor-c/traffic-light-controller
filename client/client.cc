#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#include <Magick++.h>
#include <magick/image.h>
#include <wiringPi.h>

#include "content-streamer.h"
#include "led-matrix.h"
#include "network.h"
#include "pixel-mapper.h"

constexpr bool kDoCenter = false;
constexpr uint32_t kHoldTimeMs = 125;

using tmillis_t = int64_t;

struct FileInfo {
  rgb_matrix::MemStreamIO content_stream;
  std::vector<bool> UnicolLightL[5];
  std::vector<bool> UnicolLightR[5];
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

static bool is_traffic_light_started = false;
static const Magick::Image* empty_image;
static std::vector<FileInfo*> file_imgs;
static int vsync_multiple = 1;
static rgb_matrix::RGBMatrix* matrix;
static rgb_matrix::FrameCanvas* sequence_loader_canvas = nullptr;
static rgb_matrix::FrameCanvas* offscreen_canvas;
static bool need_animation_update = false;
static int currentScenarioNum = 0;
static bool interrupt_received = false;
static uint8_t clientName =
    static_cast<uint8_t>(UserTCPprotocol::ClientName::TL12);

static void LoadScenario(std::vector<FileInfo*>& file_imgs,
                         const std::vector<int>& sequence_ids);

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

void DoCmd(uint8_t* data) {
  if (data[0] == static_cast<int>(UserTCPprotocol::Type::COMMUNICATION)) {
    if (data[1] == static_cast<int>(UserTCPprotocol::Communication::WHO)) {
      uint8_t ar[] = {0, 2, 0, clientName};
      SendToServer(ar, sizeof(ar));
    } else if (data[1] ==
               static_cast<int>(UserTCPprotocol::Communication::ACK)) {
      // uint8_t ar[] = {0, 2, 1, 1};
      // SendToServer(ar, sizeof(ar));
    }
    return;
  }

  if (data[0] == static_cast<int>(UserTCPprotocol::Type::STATE)) {
    printf("UserTCPprotocol STATE");
    if (data[1] == static_cast<int>(UserTCPprotocol::State::STOP)) {
      printf("OFF\n");
      is_traffic_light_started = false;
    } else if (data[1] == static_cast<int>(UserTCPprotocol::State::START)) {
      printf("ON\n");
      is_traffic_light_started = true;
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
      printf("UserTCPprotocol NEWCOMBINATIONS\n");
      std::vector<int> sequence_ids;
      for (int i = 0; i < 10; ++i) {
        sequence_ids.push_back(data[4 + i]);
      }
      need_animation_update = true;
      LoadScenario(file_imgs, sequence_ids);
    } else if (data[2] ==
               static_cast<int>(UserTCPprotocol::Scenario::NEXTCOMBO)) {
      if (data[3] < file_imgs.size()) {
        need_animation_update = true;
        currentScenarioNum = data[3];
      }
    }
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

void DisplayAnimation(FileInfo* file,
                      rgb_matrix::RGBMatrix* matrix,
                      rgb_matrix::FrameCanvas* offscreen_canvas,
                      int vsync_multiple) {
  rgb_matrix::StreamReader reader(&file->content_stream);
  while (!interrupt_received && !need_animation_update) {
    ReadSocketAndExecuteCommand();
    if (is_traffic_light_started) {
      uint32_t delay_us = 0;
      int seqInd = 0;
      while (!interrupt_received &&
             reader.GetNext(offscreen_canvas, &delay_us) &&
             !need_animation_update) {
        // digitalWrite(UniconLight0, file->UnicolLightL[0].at(seqInd));
        // digitalWrite(UniconLight1, file->UnicolLightL[1].at(seqInd));
        // digitalWrite(UniconLight2, file->UnicolLightL[2].at(seqInd));
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
    } else {
      file_imgs.clear();
      matrix->Clear();
    }
  }
}

static void LoadImageSequence(int sequence_id,
                              std::vector<Magick::Image>* output) {
  std::vector<Magick::Image> frames;
  if (sequence_id) {
    char image_path[256];
    std::snprintf(image_path, sizeof(image_path), "gif/%d.gif", sequence_id);
    Magick::readImages(&frames, image_path);
    printf("readImages for '%s' returned %d images\n", image_path,
           frames.size());
  }

  output->clear();
  if (frames.size() > 1) {
    Magick::coalesceImages(output, frames.begin(), frames.end());
  } else if (frames.size() == 1) {
    output->push_back(std::move(frames[0]));
  } else {
    output->push_back(*empty_image);
  }
}

/*static bool IsImageBlack(const Magick::Image& image) {
  int image_width = image.columns();
  int image_height = image.rows();
  bool isEmptry = true;
  for (int row = 0; row <= image_height; row++) {
    for (int column = 0; column <= image_width; column++) {
      Magick::ColorRGB px = image.pixelColor(column, row);
      if (px.red() > 0 || px.green() > 0 || px.blue() > 0)
        return false;
    }
  }
  return true;
}*/

static std::vector<Magick::Image> BuildRenderSequence(
    const std::vector<std::vector<Magick::Image>>& image_sequences) {
  size_t max_sequence_length = 0;
  for (int i = 0; i < 10; ++i) {
    if (image_sequences[i].size() > max_sequence_length)
      max_sequence_length = image_sequences[i].size();
  }

  std::vector<Magick::Image> result;
  for (size_t i = 0; i < max_sequence_length; ++i) {
    // Collect lists of images for left and right sides.
    std::vector<Magick::Image> stackL;
    std::vector<Magick::Image> stackR;
    for (int j = 0; j < 10; ++j) {
      const Magick::Image& image =
          (i < image_sequences[j].size() ? image_sequences[j][i]
                                         : *empty_image);
      // bool is_black = IsImageBlack(image);
      if (j >= 5) {
        // file_info->UnicolLightR[stackR.size()] = !is_black;
        stackR.push_back(image);
      } else {
        // file_info->UnicolLightL[stackL.size()] = !is_black;
        stackL.push_back(image);
      }
    }

    // Build full contiguous images for left and right.
    Magick::Image side_images[2];
    Magick::appendImages(&side_images[0], stackR.begin(), stackR.end());
    Magick::appendImages(&side_images[1], stackL.begin(), stackL.end());

    // Merge and left and right side into the single image for rendering.
    Magick::Image full_image;
    Magick::appendImages(&full_image, side_images, side_images + 2, true);

    result.push_back(std::move(full_image));
  }

  return std::move(result);
}

// |hold_time_us| indicates for how long this frame is to be shown
// in microseconds.
static void AddToMatrixStream(const Magick::Image& img,
                              uint32_t hold_time_us,
                              rgb_matrix::StreamWriter* output) {
  sequence_loader_canvas->Clear();
  const int x_offset =
      kDoCenter ? (sequence_loader_canvas->width() - img.columns()) / 2 : 0;
  const int y_offset =
      kDoCenter ? (sequence_loader_canvas->height() - img.rows()) / 2 : 0;
  for (size_t y = 0; y < img.rows(); ++y) {
    for (size_t x = 0; x < img.columns(); ++x) {
      const Magick::Color& c = img.pixelColor(x, y);
      if (c.alphaQuantum() < 256) {
        sequence_loader_canvas->SetPixel(x + x_offset, y + y_offset,
                                         ScaleQuantumToChar(c.redQuantum()),
                                         ScaleQuantumToChar(c.greenQuantum()),
                                         ScaleQuantumToChar(c.blueQuantum()));
      }
    }
  }

  output->Stream(*sequence_loader_canvas, hold_time_us);
}

static void LoadScenario(std::vector<FileInfo*>& file_imgs,
                         const std::vector<int>& sequence_ids) {
  std::vector<std::vector<Magick::Image>> image_sequences;
  for (int i = 0; i < 10; ++i) {
    int sequence_id = sequence_ids[i];
    printf("LoadScenario #%d: image %d\n", i, sequence_id);
    image_sequences.emplace_back();
    LoadImageSequence(sequence_id, &image_sequences[i]);
  }

  std::vector<Magick::Image> render_sequence =
      BuildRenderSequence(image_sequences);

  // Convert render sequence to RGB Matrix stream.
  FileInfo* file_info = new FileInfo();
  rgb_matrix::StreamWriter out(&file_info->content_stream);
  for (size_t i = 0; i < render_sequence.size(); ++i) {
    const Magick::Image& img = render_sequence[i];
    uint32_t hold_time_us = kHoldTimeMs * 1000;
    AddToMatrixStream(img, hold_time_us, &out);
  }

  file_imgs.push_back(file_info);
  printf("file_imgs size: %i\n", file_imgs.size());
}

static void SetupUnicornPins() {
  return;

  wiringPiSetup();

  pinMode(UniconLight0, OUTPUT);
  pinMode(UniconLight1, OUTPUT);
  pinMode(UniconLight2, OUTPUT);

  digitalWrite(UniconLight0, CM_OFF);
  digitalWrite(UniconLight1, CM_OFF);
  digitalWrite(UniconLight2, CM_OFF);
}

//
//------------------------MAIN----------------------------------------
//
int main(int argc, char* argv[]) {
  Magick::InitializeMagick(*argv);
  empty_image = new Magick::Image("32x32", "black");

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
          clientName = static_cast<uint8_t>(UserTCPprotocol::ClientName::TL12);
        else if (indName == 2)
          clientName = static_cast<uint8_t>(UserTCPprotocol::ClientName::TL34);
        else if (indName == 3)
          clientName = static_cast<uint8_t>(UserTCPprotocol::ClientName::TL56);
        else if (indName == 4)
          clientName = static_cast<uint8_t>(UserTCPprotocol::ClientName::TL78);
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

  SetServerAddress("192.168.88.100", 1235);

  SetupUnicornPins();

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
  sequence_loader_canvas = matrix->CreateFrameCanvas();

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);
  signal(SIGTSTP, InterruptHandler);

  fprintf(stderr, "Entering processing loop\n");

  do {
    // SleepMillis(200);
    ReadSocketAndExecuteCommand();
    if (is_traffic_light_started) {
      need_animation_update = false;
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
