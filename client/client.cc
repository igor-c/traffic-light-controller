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

#include <wiringPi.h>

#include "content-streamer.h"
#include "image.h"
#include "led-matrix.h"
#include "network.h"
#include "pixel-mapper.h"

constexpr int kVsyncMultiple = 1;

struct ScenarioStream {
  std::vector<const Animation*> animations;
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
static std::vector<ScenarioStream*> all_scenarios;
static int current_scenario_idx = -1;
static bool is_traffic_light_started = false;

static rgb_matrix::RGBMatrix* matrix;
static rgb_matrix::FrameCanvas* stream_creation_canvas = nullptr;
static bool should_interrupt_animation_loop = false;
static bool has_received_signal = false;
static uint8_t clientName =
    static_cast<uint8_t>(UserTCPprotocol::ClientName::TL12);

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
      // TODO(igorc): LoadScenario(sequence_ids);
      should_interrupt_animation_loop = true;
    } else if (data[2] ==
               static_cast<int>(UserTCPprotocol::Scenario::NEXTCOMBO)) {
      if (data[3] < all_scenarios.size()) {
        current_scenario_idx = data[3];
        should_interrupt_animation_loop = true;
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

  uint64_t next_processing_time = GetTimeInMillis();
  size_t max_frame_count = 0;
  int prev_scenario_idx = -1;
  std::vector<AnimationState> animations;
  while (!should_interrupt_animation_loop) {
    uint64_t cur_time = GetTimeInMillis();
    if (next_processing_time > cur_time) {
      SleepMillis(next_processing_time - cur_time);
      // Read commands in the time space between frames.
      ReadSocketAndExecuteCommands();
      continue;
    }

    // Just in case there's an early "continue", force the caller
    // to offload CPU since TCP read is non-blocking.
    next_processing_time = cur_time + 100;

    if (current_scenario_idx != prev_scenario_idx) {
      animations.clear();
      max_frame_count = 0;
      prev_scenario_idx = current_scenario_idx;
    }

    if (current_scenario_idx < 0) {
      // The animations have been disabled, erase all pointers.
      animations.clear();
      max_frame_count = 0;
      continue;
    }

    if (animations.empty()) {
      // Look up animation data if it's not active yet.
      for (const Animation* animation :
           all_scenarios[current_scenario_idx]->animations) {
        animations.emplace_back(animation);
        // animations.back().flip_v = true;
      }
      max_frame_count = GetMaxFrameCount(animations);
      if (!max_frame_count) {
        animations.clear();
        continue;
      }
    }

    // Render the current frame, and advance.
    next_processing_time = cur_time + kHoldTimeMs;

    // offscreen_canvas->Clear();
    if (!RenderFrame(animations, offscreen_canvas)) {
      fprintf(stderr, "Failed to render, resetting scenario\n");
      current_scenario_idx = -1;
      continue;
    }

    for (AnimationState& animation : animations) {
      animation.cur_frame++;
      if (animation.cur_frame < animation.animation->frame_count)
        continue;

      if (animation.animation->frame_count > 200) {
        // Sync all long animations, and make everyone's cur_frame
        // go to zero once it reaches max_frame_count.
        if (animation.cur_frame >= max_frame_count)
          animation.cur_frame = 0;
        continue;
      }

      animation.cur_frame = 0;
    }

    offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas, kVsyncMultiple);

    // digitalWrite(UniconLight0, scenario->UnicolLightL[0].at(seqInd));
    // digitalWrite(UniconLight1, scenario->UnicolLightL[1].at(seqInd));
    // digitalWrite(UniconLight2, scenario->UnicolLightL[2].at(seqInd));
  }

  if (!is_traffic_light_started) {
    current_scenario_idx = -1;
    for (auto* scenario : all_scenarios) {
      delete scenario;
    }
    all_scenarios.clear();
    matrix->Clear();
  }
}

static void LoadScenario(const std::string& name) {
  Collection* collection = FindCollection(name);
  if (!collection) {
    fprintf(stderr, "Unable to find collection '%s'\n", name.c_str());
    return;
  }

  std::vector<const Animation*> animations;

  const Animation* red = collection->FindAnimation("red");
  const Animation* green = collection->FindAnimation("green");
  if (red && green) {
    animations.push_back(green);
    animations.push_back(red);
    for (int i = 0; i < 8; ++i) {
      animations.push_back(green);
    }
  } else if (collection->animations.size() > 1u) {
    for (size_t i = 0; i < std::min(collection->animations.size(), 10u); ++i) {
      animations.push_back(&collection->animations[i]);
    }
    for (size_t i = collection->animations.size(); i < 10; ++i) {
      animations.push_back(&collection->animations[0]);
    }
  } else {
    for (int i = 0; i < 10; ++i) {
      const Animation& animation = collection->animations[0];
      animations.push_back(&animation);
    }
  }

  all_scenarios.push_back(new ScenarioStream());
  all_scenarios.back()->animations = animations;

  printf("Finished loading scenario '%s'\n", name.c_str());
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
  matrix_options.brightness = 70;

  // Options to eliminate visual glitches and artifacts:
  matrix_options.pwm_bits = 4;
  matrix_options.pwm_lsb_nanoseconds = 300;
  // matrix_options.pwm_dither_bits = 2;
  rgb_matrix::RuntimeOptions runtime_opt;
  runtime_opt.gpio_slowdown = 4;

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

  InitImages();

  SetRotations(std::vector<int>({-90, -90, -90, -90, -90, 90, 90, 90, 90, 90}));

  matrix = CreateMatrixFromOptions(matrix_options, runtime_opt);
  if (matrix == NULL) {
    return 1;
  }

  stream_creation_canvas = matrix->CreateFrameCanvas();

  // signal(SIGTERM, InterruptHandler);
  // signal(SIGINT, InterruptHandler);
  signal(SIGTSTP, InterruptHandler);

  // static const int demo_scenarios[] = {7, 8, 7, 8, 7, 8, 7, 8, 7, 8};
  // CreateIntBasedScenario(std::vector<int>(demo_scenarios,
  //                                         demo_scenarios + 10));

  // LoadScenario("mitya");
  // LoadScenario("pac-man");
  LoadScenario("ufo");
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
