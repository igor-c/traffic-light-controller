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

enum class TransportMode {
  NORMAL,
  TRIPPY,
};

struct Scenario {
  TransportMode transport_mode;
  AnimationState pedestrian_red;
  AnimationState pedestrian_green;
  bool is_trippy;

  Scenario(TransportMode transport_mode) : transport_mode(transport_mode) {}
};

// TODO(igorc): Implement blinking transport light.
static constexpr uint64_t kRedGreenLightTimeMs = 2 * 1000;
static constexpr uint64_t kYellowLightTimeMs = 1 * 1000;

// Scenario-related controls:
static std::vector<AnimationState> trippy_set;
static AnimationState ped_red_light_animation;
static std::vector<Scenario> all_scenarios;
static uint64_t transport_anchor_time_ms = 0;
// static int traffic_light_id = 0;
static const Scenario* scenario_main = nullptr;
static const Scenario* scenario_secondary = nullptr;
static bool is_traffic_light_started = false;

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
      // if (data[3] < all_scenarios.size()) {
      //   current_scenario_idx = data[3];
      //   should_interrupt_animation_loop = true;
      // }
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

/*static bool IsMainRoadFacing() {
  return (traffic_light_id % 2 == 0);
}*/

// Note that colors refers to those shown along the main road.
enum class LightStage {
  NOT_STARTED,
  RED,
  YELLOW,
  GREEN,
  PEDESTRIAN,
};

static LightStage GetLightStage(uint64_t cur_time) {
  if (transport_anchor_time_ms > cur_time) {
    transport_anchor_time_ms = cur_time;
  }

  uint64_t rel_time = cur_time - transport_anchor_time_ms;
  if (rel_time < kRedGreenLightTimeMs) {
    return LightStage::RED;
  } else if (rel_time < kRedGreenLightTimeMs + kYellowLightTimeMs) {
    return LightStage::YELLOW;
  } else if (rel_time < kRedGreenLightTimeMs * 2 + kYellowLightTimeMs) {
    return LightStage::GREEN;
  } else {
    return LightStage::PEDESTRIAN;
  }
}

struct LightAnimations {
  AnimationState traffic_red_main;
  AnimationState traffic_yellow_main;
  AnimationState traffic_green_main;
  AnimationState ped_red_main;
  AnimationState ped_green_main;
  AnimationState traffic_red_second;
  AnimationState traffic_yellow_second;
  AnimationState traffic_green_second;
  AnimationState ped_red_second;
  AnimationState ped_green_second;

  size_t max_ped_frame_count;
};

static void FinalizeLightAnimations(LightAnimations* lights) {
  size_t m1 = lights->ped_red_main.frame_count();
  size_t m2 = lights->ped_red_second.frame_count();
  size_t m3 = lights->ped_green_main.frame_count();
  size_t m4 = lights->ped_green_second.frame_count();
  lights->max_ped_frame_count = std::max(std::max(m1, m2), std::max(m3, m4));
}

static std::vector<AnimationState*> GetRenderSequence(LightAnimations* lights) {
  std::vector<AnimationState*> result(10, nullptr);
  result[0] = &lights->ped_green_main;
  result[1] = &lights->ped_red_main;
  result[2] = &lights->traffic_green_main;
  result[3] = &lights->traffic_yellow_main;
  result[4] = &lights->traffic_red_main;
  result[5] = &lights->traffic_red_second;
  result[6] = &lights->traffic_yellow_second;
  result[7] = &lights->traffic_green_second;
  result[8] = &lights->ped_red_second;
  result[9] = &lights->ped_green_second;
  return result;
}

static LightAnimations GetRedStateAnimations() {
  LightAnimations result;
  result.traffic_red_main = GetSolidRed();
  result.traffic_green_second = GetSolidGreen();
  result.ped_red_main = ped_red_light_animation;
  result.ped_red_second = ped_red_light_animation;
  FinalizeLightAnimations(&result);
  return result;
}

static LightAnimations GetYellowStateAnimations() {
  LightAnimations result;
  result.traffic_yellow_main = GetSolidYellow();
  result.traffic_yellow_second = GetSolidYellow();
  result.ped_red_main = ped_red_light_animation;
  result.ped_red_second = ped_red_light_animation;
  FinalizeLightAnimations(&result);
  return result;
}

static LightAnimations GetGreenStateAnimations() {
  LightAnimations result;
  result.traffic_green_main = GetSolidGreen();
  result.traffic_red_second = GetSolidRed();
  result.ped_red_main = ped_red_light_animation;
  result.ped_red_second = ped_red_light_animation;
  FinalizeLightAnimations(&result);
  return result;
}

static LightAnimations GetPedestrianStateAnimations() {
  if (scenario_main && !scenario_secondary) {
    scenario_secondary = scenario_main;
  }
  if (scenario_secondary && !scenario_main) {
    scenario_main = scenario_secondary;
  }

  if (scenario_main->is_trippy && !scenario_secondary->is_trippy) {
    scenario_secondary = scenario_main;
  }
  if (scenario_secondary->is_trippy && !scenario_main->is_trippy) {
    scenario_main = scenario_secondary;
  }

  LightAnimations result;
  if (scenario_main) {
    result.ped_red_main = scenario_main->pedestrian_red;
    result.ped_green_main = scenario_main->pedestrian_green;
  } else if (scenario_secondary) {
    result.ped_red_second = scenario_secondary->pedestrian_red;
    result.ped_green_second = scenario_secondary->pedestrian_green;
  } else {
    result.ped_green_main = GetSolidGreen();
    result.ped_green_second = GetSolidGreen();
  }

  FinalizeLightAnimations(&result);
  return result;
}

static bool RenderLightAnimations(LightAnimations* lights,
                                  rgb_matrix::FrameCanvas* canvas) {
  std::vector<AnimationState*> render_sequence = GetRenderSequence(lights);

  if (!RenderFrame(render_sequence, canvas)) {
    return false;
  }

  for (AnimationState* animation : render_sequence) {
    animation->cur_frame++;
  }

  return true;
}

static void TryRunAnimationLoop() {
  static rgb_matrix::FrameCanvas* offscreen_canvas = nullptr;
  if (!offscreen_canvas)
    offscreen_canvas = matrix->CreateFrameCanvas();

  should_interrupt_animation_loop = false;

  ReadSocketAndExecuteCommands();

  uint64_t next_processing_time = GetTimeInMillis();
  LightStage prev_stage = LightStage::NOT_STARTED;
  size_t ped_frames_shown = 0;
  LightAnimations current_lights;
  while (!should_interrupt_animation_loop) {
    uint64_t cur_time = GetTimeInMillis();
    if (next_processing_time > cur_time) {
      SleepMillis(next_processing_time - cur_time);
      // Read commands in the time space between frames.
      ReadSocketAndExecuteCommands();
      continue;
    }

    // Render the current frame, and advance.
    next_processing_time = cur_time + kHoldTimeMs;

    if (prev_stage == LightStage::PEDESTRIAN &&
        ped_frames_shown >= current_lights.max_ped_frame_count) {
      // We have just finished showing pedestrian's animations.
      // Start full cycle again.
      transport_anchor_time_ms = cur_time;
    }

    LightStage stage = GetLightStage(cur_time);
    if (stage != prev_stage) {
      switch (stage) {
        case LightStage::RED:
          current_lights = GetRedStateAnimations();
          printf("Switching to RED light\n");
          break;
        case LightStage::YELLOW:
          current_lights = GetYellowStateAnimations();
          printf("Switching to YELLOW light\n");
          break;
        case LightStage::GREEN:
          current_lights = GetGreenStateAnimations();
          printf("Switching to GREEN light\n");
          break;
        case LightStage::PEDESTRIAN:
        default:
          printf("Switching to PEDESTRIAN light\n");
          current_lights = GetPedestrianStateAnimations();
          if (current_lights.max_ped_frame_count == 1) {
            // For whatever reason - we have no animation running.
            // Just keep the colors for the regular interval.
            current_lights.max_ped_frame_count = 236;
          }
          break;
      }

      prev_stage = stage;
      ped_frames_shown = 0;
    }

    // offscreen_canvas->Clear();
    if (!RenderLightAnimations(&current_lights, offscreen_canvas)) {
      fprintf(stderr, "Failed to render, resetting current scenarios\n");
      scenario_main = nullptr;
      scenario_secondary = nullptr;
      ped_frames_shown = current_lights.max_ped_frame_count;
      continue;
    }

    ped_frames_shown++;

    static constexpr int kVsyncMultiple = 1;
    offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas, kVsyncMultiple);

    // digitalWrite(UniconLight0, scenario->UnicolLightL[0].at(seqInd));
    // digitalWrite(UniconLight1, scenario->UnicolLightL[1].at(seqInd));
    // digitalWrite(UniconLight2, scenario->UnicolLightL[2].at(seqInd));
  }

  if (!is_traffic_light_started) {
    scenario_main = nullptr;
    scenario_secondary = nullptr;
    matrix->Clear();
  }
}

static void LoadScenario(const std::string& name,
                         TransportMode transport_mode) {
  Collection* collection = FindCollection(name);
  if (!collection) {
    fprintf(stderr, "Unable to find collection '%s'\n", name.c_str());
    return;
  }

  size_t animation_count = collection->animations.size();
  printf("Loading scenario '%s' with %d animations\n", name.c_str(),
         animation_count);

  const Animation* red = collection->FindAnimation("red");
  const Animation* green = collection->FindAnimation("green");

  Scenario scenario(transport_mode);
  if (red) {
    scenario.pedestrian_red = AnimationState(red);
  }

  if (green) {
    scenario.pedestrian_green = AnimationState(green);
  }

  if (red && green) {
    if (animation_count != 2u) {
      fprintf(stderr, "Unexpected # of animations in '%s'\n", name.c_str());
    }
    all_scenarios.push_back(scenario);
    return;
  }

  if (red || green) {
    if (animation_count != 1u) {
      fprintf(stderr, "Unexpected # of animations in '%s'\n", name.c_str());
    }
    if (red) {
      scenario.pedestrian_green = scenario.pedestrian_red;
      scenario.pedestrian_green.flip_h = true;
    } else {
      scenario.pedestrian_red = scenario.pedestrian_green;
      scenario.pedestrian_red.flip_h = true;
    }
    all_scenarios.push_back(scenario);
    return;
  }

  fprintf(stderr, "Unexpected animation set in '%s'\n", name.c_str());
}

static std::vector<AnimationState> LoadAnimationSet(const std::string& name) {
  std::vector<AnimationState> result;
  Collection* collection = FindCollection(name);
  if (!collection) {
    fprintf(stderr, "Unable to find collection '%s'\n", name.c_str());
    return result;
  }

  for (size_t i = 0; i < collection->animations.size(); ++i) {
    AnimationState state(&collection->animations[i]);
    result.push_back(state);
  }

  return result;
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

static void SetupScenarios() {
  transport_anchor_time_ms = GetTimeInMillis();

  trippy_set = LoadAnimationSet("mitya");

  // static const int demo_scenarios[] = {7, 8, 7, 8, 7, 8, 7, 8, 7, 8};
  // CreateIntBasedScenario(std::vector<int>(demo_scenarios,
  //                                         demo_scenarios + 10));

  LoadScenario("bike", TransportMode::NORMAL);
  LoadScenario("burning", TransportMode::NORMAL);
  // LoadScenario("dance", TransportMode::NORMAL);
  LoadScenario("hug", TransportMode::NORMAL);
  LoadScenario("lsd", TransportMode::TRIPPY);
  LoadScenario("meditation", TransportMode::NORMAL);
  LoadScenario("pac-man", TransportMode::NORMAL);
  // LoadScenario("party", TransportMode::FULL);
  LoadScenario("pray", TransportMode::NORMAL);
  LoadScenario("rastaman", TransportMode::NORMAL);
  LoadScenario("recursion", TransportMode::NORMAL);
  // LoadScenario("sex", TransportMode::NORMAL);
  LoadScenario("ufo", TransportMode::NORMAL);

  ped_red_light_animation = GetSolidRed();

  Collection* still_images = FindCollection("still");
  if (still_images) {
    const Animation* animation = still_images->FindAnimation("stop_cat");
    if (animation) {
      ped_red_light_animation = AnimationState(animation);
      ped_red_light_animation.rotation = -90;
      printf("Using stop_cat.gif for pedestrians\n");
    }
  }

  scenario_main = &all_scenarios[0];
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

  SetRotations(std::vector<int>({-90, -90, -90, -90, -90, 90, 90, 90, 90, 90}));
  InitImages();

  SetupScenarios();

  matrix = CreateMatrixFromOptions(matrix_options, runtime_opt);
  if (matrix == NULL) {
    return 1;
  }

  stream_creation_canvas = matrix->CreateFrameCanvas();

  // signal(SIGTERM, InterruptHandler);
  // signal(SIGINT, InterruptHandler);
  signal(SIGTSTP, InterruptHandler);

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
