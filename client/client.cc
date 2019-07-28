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
constexpr int kVsyncMultiple = 1;

struct Scenario {
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

// Scenario-related controls:
static std::vector<Scenario*> all_scenarios;
static int current_scenario_idx = -1;
static bool is_traffic_light_started = false;

static const Magick::Image* empty_image;
static rgb_matrix::RGBMatrix* matrix;
static bool should_interrupt_animation_loop = false;
static bool has_received_signal = false;
static uint8_t clientName =
    static_cast<uint8_t>(UserTCPprotocol::ClientName::TL12);

static void LoadScenario(const std::vector<int>& sequence_ids);

//------------------------SERVER-------------------------------------------

static uint64_t GetTimeInMillis() {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

static void SleepMillis(uint64_t milli_seconds) {
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
    should_interrupt_animation_loop = true;
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
      should_interrupt_animation_loop = true;
      LoadScenario(sequence_ids);
    } else if (data[2] ==
               static_cast<int>(UserTCPprotocol::Scenario::NEXTCOMBO)) {
      if (data[3] < all_scenarios.size()) {
        should_interrupt_animation_loop = true;
        current_scenario_idx = data[3];
      }
    }
  }
}

void ReadSocketAndExecuteCommands() {
  while (true) {
    std::vector<uint8_t> command = ReadSocketCommand();
    if (command.empty())
      break;

    DoCmd(command.data());
  }
}

static void InterruptHandler(int signo) {
  has_received_signal = true;
  should_interrupt_animation_loop = true;
}

static void TryRunAnimationLoop() {
  static rgb_matrix::FrameCanvas* offscreen_canvas = nullptr;
  if (!offscreen_canvas)
    offscreen_canvas = matrix->CreateFrameCanvas();

  should_interrupt_animation_loop = false;

  ReadSocketAndExecuteCommands();

  bool has_drawn = false;
  while (!should_interrupt_animation_loop && current_scenario_idx >= 0) {
    uint32_t hold_time_us = 0;
    // Make a copy of scenario since a new one can be added,
    // relocating all the data in memory.
    Scenario* scenario = all_scenarios[current_scenario_idx];
    rgb_matrix::StreamReader reader(&scenario->content_stream);
    while (!should_interrupt_animation_loop &&
           reader.GetNext(offscreen_canvas, &hold_time_us)) {
      uint64_t deadline_time = GetTimeInMillis() + hold_time_us / 1000;

      // digitalWrite(UniconLight0, scenario->UnicolLightL[0].at(seqInd));
      // digitalWrite(UniconLight1, scenario->UnicolLightL[1].at(seqInd));
      // digitalWrite(UniconLight2, scenario->UnicolLightL[2].at(seqInd));

      offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas, kVsyncMultiple);

      ReadSocketAndExecuteCommands();

      has_drawn = true;
      uint64_t cur_time = GetTimeInMillis();
      if (cur_time < deadline_time)
        SleepMillis(deadline_time - cur_time);
    }

    ReadSocketAndExecuteCommands();
  }

  if (!is_traffic_light_started) {
    current_scenario_idx = -1;
    for (auto* scenario : all_scenarios) {
      delete scenario;
    }
    all_scenarios.clear();
    matrix->Clear();
  }

  if (!has_drawn) {
    // Force the caller to offload CPU since TCP read is non-blocking.
    SleepMillis(100);
  }
}

static void LoadImageSequence(int sequence_id,
                              std::vector<Magick::Image>* output) {
  std::vector<Magick::Image> frames;
  if (sequence_id) {
    char image_path[256];
    std::snprintf(image_path, sizeof(image_path), "gif/%d.gif", sequence_id);
    try {
      Magick::readImages(&frames, image_path);
    } catch (Magick::ErrorFileOpen& e) {
      // Ignore.
    }
    printf("readImages for '%s' returned %d images\n", image_path,
           frames.size());
  }

  output->clear();
  if (frames.size() > 1) {
    // Unpack an animated GIF into a series of same-size frames.
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
    const std::vector<int>& sequence_ids) {
  std::vector<std::vector<Magick::Image>> image_sequences;
  for (int i = 0; i < 10; ++i) {
    int sequence_id = sequence_ids[i];
    printf("Loading scenario #%d: image %d\n", i, sequence_id);
    image_sequences.emplace_back();
    LoadImageSequence(sequence_id, &image_sequences[i]);
  }

  // Calculate the longest sequence length.
  size_t max_sequence_length = 0;
  for (int i = 0; i < 10; ++i) {
    if (image_sequences[i].size() > max_sequence_length)
      max_sequence_length = image_sequences[i].size();
  }

  // Fill shorter sequences, so they all become the same size.
  for (int i = 0; i < 10; ++i) {
    size_t missing_count = max_sequence_length - image_sequences[i].size();
    for (size_t j = 0; j < missing_count; ++j) {
      image_sequences[i].push_back(*empty_image);
    }
  }

  // For every step in sequence - build the entire rendered image.
  fprintf(stderr, "Building rendering sequence, max_length=%d\n",
          (int)max_sequence_length);
  std::vector<Magick::Image> result;
  for (size_t i = 0; i < max_sequence_length; ++i) {
    // In this sequence step - collect images for left and right sides.
    std::vector<Magick::Image> images_left;
    std::vector<Magick::Image> images_right;
    for (int j = 0; j < 10; ++j) {
      std::vector<Magick::Image>& images_side =
          (j >= 5 ? images_right : images_left);
      images_side.push_back(image_sequences[j][i]);

      // Decrease ref count in the original image, so it doesn't need
      // to be cloned inside appendImages() or rotate() below.
      image_sequences[j][i] = *empty_image;

      // The panels layout is vertical, connected bottom-to-top, with the input
      // at the bottom. RGNMatrix expects input on the right side.
      // Rotate 90CCW to make things align properly.
      images_side.back().rotate(-90);

      // bool is_black = IsImageBlack(image);
      // scenario->UnicolLightR[images_right.size()] = !is_black;
      // scenario->UnicolLightL[images_left.size()] = !is_black;
    }

    // The overall matrix contains 10 chained panels.

    // Reorganize images from bottom-to-top left and right side lists,
    // to 2 left-to-right contiguous images. One image now contains
    // data for 5 panels.
    Magick::Image side_images[2];
    Magick::appendImages(&side_images[0], images_right.begin(),
                         images_right.end());
    Magick::appendImages(&side_images[1], images_left.begin(),
                         images_left.end());

    // Merge left and right sides into the single image for rendering.
    // One image now contains data for all 10 panels.
    result.emplace_back();
    Magick::Image& full_image = result.back();
    Magick::appendImages(&full_image, side_images, side_images + 2);
  }

  return std::move(result);
}

// |hold_time_us| indicates for how long this frame is to be shown
// in microseconds.
static void AddToMatrixStream(const Magick::Image& img,
                              uint32_t hold_time_us,
                              rgb_matrix::StreamWriter* output) {
  static rgb_matrix::FrameCanvas* canvas = nullptr;
  if (!canvas)
    canvas = matrix->CreateFrameCanvas();

  canvas->Clear();
  const int x_offset = kDoCenter ? (canvas->width() - img.columns()) / 2 : 0;
  const int y_offset = kDoCenter ? (canvas->height() - img.rows()) / 2 : 0;
  for (size_t y = 0; y < img.rows(); ++y) {
    for (size_t x = 0; x < img.columns(); ++x) {
      const Magick::Color& c = img.pixelColor(x, y);
      if (c.alphaQuantum() < 256) {
        canvas->SetPixel(x + x_offset, y + y_offset,
                         ScaleQuantumToChar(c.redQuantum()),
                         ScaleQuantumToChar(c.greenQuantum()),
                         ScaleQuantumToChar(c.blueQuantum()));
      }
    }
  }

  output->Stream(*canvas, hold_time_us);
}

static void LoadScenario(const std::vector<int>& sequence_ids) {
  fprintf(stderr, "LoadScenario 1\n");
  std::vector<Magick::Image> render_sequence =
      BuildRenderSequence(sequence_ids);

  fprintf(stderr, "Loading rendering sequences, count=%d\n",
          (int)render_sequence.size());
  // Convert render sequence to RGB Matrix stream.
  all_scenarios.push_back(new Scenario());
  rgb_matrix::StreamWriter out(&all_scenarios.back()->content_stream);
  for (size_t i = 0; i < render_sequence.size(); ++i) {
    fprintf(stderr, "LoadScenario %d/%d\n", (int)i,
            (int)render_sequence.size());
    const Magick::Image& img = render_sequence[i];
    uint32_t hold_time_us = kHoldTimeMs * 1000;
    AddToMatrixStream(img, hold_time_us, &out);
  }

  printf("Finished loading scenario, scenario count = %d\n",
         (int)all_scenarios.size());
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

  rgb_matrix::RGBMatrix::Options matrix_options;
  matrix_options.rows = 32;
  matrix_options.cols = 32;
  matrix_options.chain_length = 10;
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

  matrix = CreateMatrixFromOptions(matrix_options, runtime_opt);
  if (matrix == NULL) {
    return 1;
  }

  // signal(SIGTERM, InterruptHandler);
  // signal(SIGINT, InterruptHandler);
  signal(SIGTSTP, InterruptHandler);

  static const int demo_scenarios[] = {7, 8, 7, 8, 7, 8, 7, 8, 7, 8};
  LoadScenario(std::vector<int>(demo_scenarios, demo_scenarios + 10));
  current_scenario_idx = 0;

  fprintf(stderr, "Entering processing loop\n");

  do {
    TryRunAnimationLoop();
  } while (!has_received_signal);

  if (has_received_signal) {
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
