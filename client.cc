#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "image.h"
#include "led-matrix.h"
#include "network.h"
#include "pixel-mapper.h"
#include "utils.h"

struct ClientConfig {
  std::string app_path;
  bool is_matrix_chinese = false;
  // TODO(igorc): Implement blinking transport light.
  uint64_t red_green_light_ms = 20000;
  uint64_t yellow_light_ms = 3000;
  int traffic_light_id = 0;
  bool is_sequential = false;
  std::string first_scenario;
  // std::map<std::string, std::string>
};

struct ScenarioSpec {
  std::string name;
  std::string ped_move_up;
  std::string ped_move_down;
  std::string ped_stop_up;
  std::string ped_stop_down;
  std::string traffic_random;
  std::string traffic_up;
  std::string traffic_middle;
  std::string traffic_down;
  bool full_random = false;

  ScenarioSpec(const std::string& name,
               const std::string& ped_move_up,
               const std::string& ped_move_down)
      : name(name), ped_move_up(ped_move_up), ped_move_down(ped_move_down) {}
};

struct Scenario {
  std::string name;
  AnimationState ped_move_up;
  AnimationState ped_move_down;
  AnimationState ped_stop_up;
  AnimationState ped_stop_down;
  AnimationState traffic_up;
  AnimationState traffic_middle;
  AnimationState traffic_down;
  std::vector<AnimationState> traffic_random;
  bool full_random = false;

  Scenario(const std::string& name) : name(name) {}
};

struct UdpMessage {
  uint64_t time_relative_to_anchor_ms;
  char next_scenario[256];
  uint8_t next_has_stop_cat;
  uint8_t is_override;
};

static ClientConfig client_config;

// Scenario-related controls:
static std::vector<Scenario> all_scenarios;
static uint64_t transport_anchor_time_ms = 0;
static const Scenario* scenario_main = nullptr;
static bool scenario_main_stop_cat = false;
static bool is_traffic_light_started = false;

static const Scenario* next_scenario = nullptr;
static bool next_has_stop_cat = false;
static bool next_has_override = false;

static AnimationState red_light_animation;
static AnimationState stop_cat_animation;

static rgb_matrix::RGBMatrix* matrix;
static bool should_interrupt_animation_loop = false;
static bool has_received_signal = false;

static const Scenario* FindScenario(const std::string& name);

constexpr uint64_t kNoTimeDiff = 11111111;

//------------------------SERVER-------------------------------------------

static void quit(int val) {
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

static void ProcessUdpCommand(const UdpMessage& msg) {
  uint64_t old_anchor = transport_anchor_time_ms;

  if (msg.time_relative_to_anchor_ms != kNoTimeDiff) {
    // Also compensate remote time for average processing delay.
    transport_anchor_time_ms =
        CurrentTimeMillis() - msg.time_relative_to_anchor_ms - kHoldTimeMs / 2;
  }

  int time_diff = 0;
  if (transport_anchor_time_ms > old_anchor) {
    time_diff = transport_anchor_time_ms - old_anchor;
  } else if (transport_anchor_time_ms < old_anchor) {
    time_diff = -(old_anchor - transport_anchor_time_ms);
  }

  printf(
      "Received UDP scenario '%s', stop_cat=%d, override=%d, anchor_diff=%d\n",
      msg.next_scenario, (int)msg.next_has_stop_cat, (int)msg.is_override,
      time_diff);

  next_scenario = FindScenario(msg.next_scenario);
  if (!next_scenario) {
    fprintf(stderr, "Unknown UDP scenario name %s\n", msg.next_scenario);
  }

  next_has_stop_cat = msg.next_has_stop_cat;
  next_has_override = msg.is_override;
}

void ReadSocketAndExecuteCommands() {
  while (true) {
    std::vector<uint8_t> command = ReadSocketCommand();
    if (command.empty())
      break;

    // DoCmd(command.data());
  }

  uint8_t* last_udp_data = nullptr;
  size_t last_udp_size = 0;
  while (true) {
    size_t udp_size = 0;
    uint8_t* udp_data = ReceiveUdp(&udp_size);
    if (!udp_data || !udp_size)
      break;

    last_udp_data = udp_data;
    last_udp_size = udp_size;
  }

  if (last_udp_data && last_udp_size) {
    if (last_udp_size != sizeof(UdpMessage)) {
      fprintf(stderr, "Wrong UDP message size %d\n", (int)last_udp_size);
      return;
    }

    UdpMessage* msg = (UdpMessage*)last_udp_data;
    ProcessUdpCommand(*msg);
  }
}

static void InterruptHandler(int signo) {
  has_received_signal = true;
  should_interrupt_animation_loop = true;
}

static bool IsMainRoadFacing() {
  return (client_config.traffic_light_id % 2 == 0);
}

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
  if (rel_time < client_config.red_green_light_ms) {
    return LightStage::RED;
  } else if (rel_time <
             client_config.red_green_light_ms + client_config.yellow_light_ms) {
    return LightStage::YELLOW;
  } else if (rel_time < client_config.red_green_light_ms * 2 +
                            client_config.yellow_light_ms) {
    return LightStage::GREEN;
  } else {
    return LightStage::PEDESTRIAN;
  }
}

struct LightAnimations {
  AnimationState side1_traffic_up;
  AnimationState side1_traffic_middle;
  AnimationState side1_traffic_down;
  AnimationState side1_pedestrian_up;
  AnimationState side1_pedestrian_down;
  AnimationState side2_traffic_up;
  AnimationState side2_traffic_middle;
  AnimationState side2_traffic_down;
  AnimationState side2_pedestrian_up;
  AnimationState side2_pedestrian_down;

  std::vector<AnimationState> traffic_random;
  bool full_random = false;

  bool has_future_pedestrian = false;
  AnimationState future_pedestrian_up;
  AnimationState future_pedestrian_down;

  size_t max_pedestrian_frame_count;
};

static void FinalizeLightAnimations(LightAnimations* lights) {
  size_t m1 = lights->side1_pedestrian_up.frame_count();
  size_t m2 = lights->side2_pedestrian_up.frame_count();
  size_t m3 = lights->side1_pedestrian_down.frame_count();
  size_t m4 = lights->side2_pedestrian_down.frame_count();
  lights->max_pedestrian_frame_count =
      std::max(std::max(m1, m2), std::max(m3, m4));
}

static std::vector<AnimationState*> GetRenderSequence(LightAnimations* lights) {
  std::vector<AnimationState*> result(10, nullptr);
  result[0] = &lights->side1_pedestrian_down;
  result[1] = &lights->side1_pedestrian_up;
  result[2] = &lights->side1_traffic_down;
  result[3] = &lights->side1_traffic_middle;
  result[4] = &lights->side1_traffic_up;
  result[5] = &lights->side2_traffic_up;
  result[6] = &lights->side2_traffic_middle;
  result[7] = &lights->side2_traffic_down;
  result[8] = &lights->side2_pedestrian_up;
  result[9] = &lights->side2_pedestrian_down;
  return result;
}

static void ApplyFuturePedestrian(LightAnimations* dst,
                                  const LightAnimations& prev_ped_lights) {
  if (prev_ped_lights.has_future_pedestrian) {
    dst->side1_pedestrian_up = prev_ped_lights.future_pedestrian_up;
    dst->side1_pedestrian_down = prev_ped_lights.future_pedestrian_down;
    dst->side2_pedestrian_up = prev_ped_lights.future_pedestrian_up;
    dst->side2_pedestrian_down = prev_ped_lights.future_pedestrian_down;
  }
}

static LightAnimations GetRedStateAnimations(
    const LightAnimations& prev_ped_lights) {
  LightAnimations result;
  if (IsMainRoadFacing()) {
    result.side1_traffic_up = GetSolidRed();
    result.side2_traffic_up = GetSolidRed();
  } else {
    result.side1_traffic_down = GetSolidGreen();
    result.side2_traffic_down = GetSolidGreen();
  }
  result.side1_pedestrian_up = red_light_animation;
  result.side2_pedestrian_up = red_light_animation;
  ApplyFuturePedestrian(&result, prev_ped_lights);
  FinalizeLightAnimations(&result);
  return result;
}

static LightAnimations GetYellowStateAnimations(
    const LightAnimations& prev_ped_lights) {
  LightAnimations result;
  result.side1_traffic_middle = GetSolidYellow();
  result.side2_traffic_middle = GetSolidYellow();
  result.side1_pedestrian_up = red_light_animation;
  result.side2_pedestrian_up = red_light_animation;
  ApplyFuturePedestrian(&result, prev_ped_lights);
  FinalizeLightAnimations(&result);
  return result;
}

static LightAnimations GetGreenStateAnimations(
    const LightAnimations& prev_ped_lights) {
  LightAnimations result;
  if (IsMainRoadFacing()) {
    result.side1_traffic_down = GetSolidGreen();
    result.side2_traffic_down = GetSolidGreen();
  } else {
    result.side1_traffic_up = GetSolidRed();
    result.side2_traffic_up = GetSolidRed();
  }
  result.side1_pedestrian_up = red_light_animation;
  result.side2_pedestrian_up = red_light_animation;
  ApplyFuturePedestrian(&result, prev_ped_lights);
  FinalizeLightAnimations(&result);
  return result;
}

static LightAnimations GetPedestrianStateAnimations() {
  const Scenario* scenario1 = scenario_main;
  const Scenario* scenario2 = scenario_main;

  LightAnimations result;
  if (!scenario1 && !scenario2) {
    result.side1_traffic_up = GetSolidRed();
    result.side2_traffic_up = GetSolidRed();
    result.side1_pedestrian_down = GetSolidGreen();
    result.side2_pedestrian_down = GetSolidGreen();
    FinalizeLightAnimations(&result);
    return result;
  }

  if (scenario1 && !scenario2) {
    scenario2 = scenario1;
  }
  if (scenario2 && !scenario1) {
    scenario1 = scenario2;
  }

  result.side1_pedestrian_up = scenario1->ped_move_up;
  result.side1_pedestrian_down = scenario1->ped_move_down;
  result.side1_traffic_up = scenario1->traffic_up;
  result.side1_traffic_middle = scenario1->traffic_middle;
  result.side1_traffic_down = scenario1->traffic_down;

  result.side2_pedestrian_up = scenario2->ped_move_up;
  result.side2_pedestrian_down = scenario2->ped_move_down;
  result.side2_traffic_up = scenario2->traffic_up;
  result.side2_traffic_middle = scenario2->traffic_middle;
  result.side2_traffic_down = scenario2->traffic_down;

  if (scenario1->full_random) {
    result.full_random = scenario1->full_random;
    result.traffic_random = scenario1->traffic_random;
  } else if (!scenario1->traffic_random.empty()) {
    result.traffic_random = scenario1->traffic_random;
  } else if (!scenario2->traffic_random.empty()) {
    result.traffic_random = scenario2->traffic_random;
  }

  // Set continuing animations on pedestrian lights, if selected.
  if (scenario1->ped_stop_up || scenario1->ped_stop_down) {
    result.future_pedestrian_up = scenario1->ped_stop_up;
    result.future_pedestrian_down = scenario1->ped_stop_down;
    result.has_future_pedestrian = true;
  } else if (scenario2->ped_stop_up || scenario2->ped_stop_down) {
    result.future_pedestrian_up = scenario2->ped_stop_up;
    result.future_pedestrian_down = scenario2->ped_stop_down;
    result.has_future_pedestrian = true;
  } else if (scenario_main_stop_cat && stop_cat_animation) {
    result.future_pedestrian_up = stop_cat_animation;
    result.has_future_pedestrian = true;
  }

  // Set the red traffic light, if it's not overtaken.
  if (!result.side1_traffic_up && !result.side1_traffic_middle &&
      !result.side1_traffic_down) {
    result.side1_traffic_up = GetSolidRed();
  }
  if (!result.side2_traffic_up && !result.side2_traffic_middle &&
      !result.side2_traffic_down) {
    result.side2_traffic_up = GetSolidRed();
  }

  FinalizeLightAnimations(&result);
  return result;
}

static void UpdateRandomTrafficLight(AnimationState* state,
                                     LightAnimations* current_lights) {
  size_t frame_count = state->frame_count();
  if (frame_count > 2 && state->cur_frame < frame_count)
    return;

  size_t next_idx = GetRandom(0, current_lights->traffic_random.size() - 1);
  *state = current_lights->traffic_random[next_idx];
}

static void UpdateRandomTrafficLights(LightAnimations* current_lights,
                                      size_t ped_frames_shown) {
  if (current_lights->traffic_random.empty()) {
    return;
  }

  if (current_lights->full_random) {
    if (current_lights->side1_pedestrian_up)
      return;
  } else {
    // Wait for 5.5 seconds for the trip to start.
    if (ped_frames_shown < 5500 / kHoldTimeMs) {
      return;
    }
  }

  UpdateRandomTrafficLight(&current_lights->side1_pedestrian_up,
                           current_lights);
  UpdateRandomTrafficLight(&current_lights->side2_pedestrian_up,
                           current_lights);

  if (current_lights->full_random) {
    UpdateRandomTrafficLight(&current_lights->side1_pedestrian_down,
                             current_lights);
    UpdateRandomTrafficLight(&current_lights->side2_pedestrian_down,
                             current_lights);
  }

  UpdateRandomTrafficLight(&current_lights->side1_traffic_up, current_lights);
  UpdateRandomTrafficLight(&current_lights->side1_traffic_middle,
                           current_lights);
  UpdateRandomTrafficLight(&current_lights->side1_traffic_down, current_lights);
  UpdateRandomTrafficLight(&current_lights->side2_traffic_up, current_lights);
  UpdateRandomTrafficLight(&current_lights->side2_traffic_middle,
                           current_lights);
  UpdateRandomTrafficLight(&current_lights->side2_traffic_down, current_lights);
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

static const Scenario* FindScenario(const std::string& name) {
  for (size_t i = 0; i < all_scenarios.size(); ++i) {
    if (all_scenarios[i].name == name)
      return &all_scenarios[i];
  }
  return nullptr;
}

static void Broadcast(const std::string& scenario_name,
                      bool has_stop_cat,
                      bool is_override) {
  UdpMessage msg;
  if (is_override) {
    msg.time_relative_to_anchor_ms = kNoTimeDiff;
  } else {
    msg.time_relative_to_anchor_ms =
        CurrentTimeMillis() - transport_anchor_time_ms;
  }

  msg.next_has_stop_cat = has_stop_cat;
  msg.is_override = is_override;

  strcpy(msg.next_scenario, scenario_name.c_str());

  printf("Broadcasting picked scenario '%s', stop_cat=%d, stagger=%d\n",
         msg.next_scenario, (int)msg.next_has_stop_cat,
         (int)(CurrentTimeMillis() - transport_anchor_time_ms));

  BroadcastUdp(&msg, sizeof(msg), is_override);
}

static bool IsEnabled(const Scenario* scenario) {
  if (!scenario)
    return false;

  if (scenario->name == "birthday") {
    time_t t = time(nullptr);
    struct tm tm = *localtime(&t);
    if (tm.tm_mon + 1 == 9 && tm.tm_mday == 1) {
      return true;
    }

    return false;
  }

  return true;
}

static void PickNextPedestrian() {
  if (next_scenario) {
    scenario_main = next_scenario;
    scenario_main_stop_cat = next_has_stop_cat;
    next_scenario = nullptr;
    next_has_stop_cat = false;
    next_has_override = false;
    return;
  }

  if (!scenario_main && !client_config.first_scenario.empty()) {
    scenario_main = FindScenario(client_config.first_scenario);
  } else {
    for (int i = 0; i < 50; ++i) {
      if (client_config.is_sequential) {
        scenario_main =
            PickNextSequential<Scenario>(all_scenarios, scenario_main);
      } else {
        scenario_main = PickNextRandom<Scenario>(all_scenarios, scenario_main);
      }

      if (IsEnabled(scenario_main))
        break;
    }
  }

  scenario_main_stop_cat = IsRandomPercentile(9);

  if (scenario_main) {
    Broadcast(scenario_main->name, scenario_main_stop_cat, false);
  } else {
    Broadcast("", scenario_main_stop_cat, false);
  }
}

static void TryRunAnimationLoop() {
  static rgb_matrix::FrameCanvas* offscreen_canvas = nullptr;
  if (!offscreen_canvas)
    offscreen_canvas = matrix->CreateFrameCanvas();

  should_interrupt_animation_loop = false;

  ReadSocketAndExecuteCommands();

  uint64_t next_processing_time = CurrentTimeMillis();
  uint64_t next_pedestrian_switch_time = 0;  // Wait for red light to decide.
  LightStage prev_stage = LightStage::NOT_STARTED;
  size_t ped_frames_shown = 0;
  LightAnimations current_lights;
  LightAnimations prev_ped_lights;
  while (!should_interrupt_animation_loop) {
    fflush(stdout);
    fflush(stderr);

    uint64_t cur_time = CurrentTimeMillis();
    if (next_processing_time > cur_time) {
      SleepMillis(next_processing_time - cur_time);
      // Read commands in the time space between frames.
      ReadSocketAndExecuteCommands();
      continue;
    }

    // Render the current frame, and advance.
    next_processing_time = cur_time + kHoldTimeMs;

    if (prev_stage == LightStage::PEDESTRIAN &&
        ped_frames_shown >= current_lights.max_pedestrian_frame_count) {
      // We have just finished showing pedestrian's animations.
      // Start full cycle again.
      transport_anchor_time_ms = cur_time;
    }

    if (next_pedestrian_switch_time &&
        cur_time >= next_pedestrian_switch_time) {
      PickNextPedestrian();
      next_pedestrian_switch_time = 0;
    }

    LightStage stage = GetLightStage(cur_time);
    if (stage != prev_stage) {
      switch (stage) {
        case LightStage::RED: {
          current_lights = GetRedStateAnimations(prev_ped_lights);
          uint64_t switch_delay_ms =
              GetRandom(0, (size_t)client_config.red_green_light_ms);
          next_pedestrian_switch_time = cur_time + switch_delay_ms;
          printf("Switching to RED light, scenario switch delay is %dms\n",
                 (int)switch_delay_ms);
          break;
        }
        case LightStage::YELLOW:
          current_lights = GetYellowStateAnimations(prev_ped_lights);
          printf("Switching to YELLOW light\n");
          break;
        case LightStage::GREEN:
          current_lights = GetGreenStateAnimations(prev_ped_lights);
          printf("Switching to GREEN light\n");
          break;
        case LightStage::PEDESTRIAN:
        default: {
          current_lights = GetPedestrianStateAnimations();
          printf(
              "Switching to PEDESTRIAN light, scenario '%s', "
              "future_ped='%s'\n",
              (scenario_main ? scenario_main->name.c_str() : "<none>"),
              (current_lights.future_pedestrian_up
                   ? current_lights.future_pedestrian_up.animation->name.c_str()
                   : ""));
          if (current_lights.max_pedestrian_frame_count == 1 ||
              current_lights.full_random) {
            // For whatever reason - we have no animation running.
            // Just keep the colors for the regular interval.
            current_lights.max_pedestrian_frame_count = 236;
          }
          prev_ped_lights = current_lights;
          break;
        }
      }

      prev_stage = stage;
      ped_frames_shown = 0;
    }

    UpdateRandomTrafficLights(&current_lights, ped_frames_shown);

    // offscreen_canvas->Clear();
    if (!RenderLightAnimations(&current_lights, offscreen_canvas)) {
      fprintf(stderr, "Failed to render, resetting current scenarios\n");
      scenario_main = nullptr;
      ped_frames_shown = current_lights.max_pedestrian_frame_count;
      continue;
    }

    ped_frames_shown++;

    static constexpr int kVsyncMultiple = 1;
    offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas, kVsyncMultiple);
  }

  if (!is_traffic_light_started) {
    scenario_main = nullptr;
    matrix->Clear();
  }
}

static void SetupCommonScenarioData() {
  transport_anchor_time_ms = CurrentTimeMillis();

  red_light_animation = GetSolidRed();

  Collection* still_images = FindCollection("still");
  if (still_images) {
    const Animation* animation = still_images->FindAnimation("stop_cat");
    if (animation) {
      stop_cat_animation = AnimationState(animation);
      stop_cat_animation.rotation = -90;
      printf("Found stop_cat.gif\n");
    }
  }
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
    state.is_cyclic = true;
    if (collection->name == "birthday") {
      state.rotation = -90;
    }
    result.push_back(state);
  }

  return result;
}

static bool LoadOneAnimation(AnimationState* dst,
                             const Collection* collection,
                             const std::string& name) {
  *dst = AnimationState();
  if (name.empty()) {
    return true;
  }

  dst->animation = collection->FindAnimation(name);
  if (!dst->animation) {
    printf("Unable to find animation '%s' in '%s'\n", name.c_str(),
           collection->name.c_str());
    return false;
  }

  if (collection->name == "birthday") {
    dst->rotation = -90;
  }

  return true;
}

static void LoadScenario(const ScenarioSpec& spec) {
  Collection* collection = FindCollection(spec.name.c_str());
  if (!collection) {
    fprintf(stderr, "Unable to find collection '%s'\n", spec.name.c_str());
    return;
  }

  printf("Loading scenario '%s'\n", spec.name.c_str());

  all_scenarios.emplace_back(spec.name);
  Scenario& scenario = all_scenarios.back();

  bool success = true;
  if (!LoadOneAnimation(&scenario.ped_move_up, collection, spec.ped_move_up)) {
    success = false;
  }
  if (!LoadOneAnimation(&scenario.ped_move_down, collection,
                        spec.ped_move_down)) {
    success = false;
  }
  if (!LoadOneAnimation(&scenario.ped_stop_up, collection, spec.ped_stop_up)) {
    success = false;
  }
  if (!LoadOneAnimation(&scenario.ped_stop_down, collection,
                        spec.ped_stop_down)) {
    success = false;
  }
  if (!LoadOneAnimation(&scenario.traffic_up, collection, spec.traffic_up)) {
    success = false;
  }
  if (!LoadOneAnimation(&scenario.traffic_middle, collection,
                        spec.traffic_middle)) {
    success = false;
  }
  if (!LoadOneAnimation(&scenario.traffic_down, collection,
                        spec.traffic_down)) {
    success = false;
  }

  scenario.full_random = spec.full_random;
  if (scenario.full_random) {
    scenario.traffic_random = LoadAnimationSet(spec.name);
    if (scenario.traffic_random.empty()) {
      fprintf(stderr, "Unable to find animation set '%s'\n", spec.name.c_str());
      success = false;
    }
  } else if (!spec.traffic_random.empty()) {
    scenario.traffic_random = LoadAnimationSet(spec.traffic_random);
    if (scenario.traffic_random.empty()) {
      fprintf(stderr, "Unable to find animation set '%s'\n",
              spec.traffic_random.c_str());
      success = false;
    }
  }

  scenario.traffic_up.is_cyclic = true;
  scenario.traffic_middle.is_cyclic = true;
  scenario.traffic_down.is_cyclic = true;

  if (scenario.traffic_random.empty()) {
    if (scenario.ped_move_up && !scenario.ped_move_down) {
      scenario.ped_move_down = scenario.ped_move_up;
      scenario.ped_move_down.flip_h = true;
    } else if (scenario.ped_move_down && !scenario.ped_move_up) {
      scenario.ped_move_up = scenario.ped_move_down;
      scenario.ped_move_up.flip_h = true;
    }
  }

  if (!success) {
    fprintf(stderr, "Failed to load scenario '%s'\n", spec.name.c_str());
    all_scenarios.pop_back();
  }
}

static void LoadAllScenarios() {
  SetupCommonScenarioData();

  LoadScenario(ScenarioSpec("bike", "", "red"));
  LoadScenario(ScenarioSpec("burning", "", "red"));
  LoadScenario(ScenarioSpec("hug", "red", "green"));
  LoadScenario(ScenarioSpec("pac-man", "", "green"));

  ScenarioSpec lsd("lsd", "", "green");
  lsd.traffic_random = "mitya";
  LoadScenario(lsd);

  LoadScenario(ScenarioSpec("pray", "", "red"));
  LoadScenario(ScenarioSpec("rastaman", "red", "green"));
  LoadScenario(ScenarioSpec("recursion", "", "green"));
  LoadScenario(ScenarioSpec("ufo", "red", "green"));
  LoadScenario(ScenarioSpec("sex", "", "red"));

  ScenarioSpec dance("dance", "green_up", "green_down");
  dance.ped_stop_up = "red_up";
  dance.ped_stop_down = "red_down";
  LoadScenario(dance);

  ScenarioSpec party("party", "red", "green");
  party.traffic_up = "equalizer_up";
  party.traffic_middle = "equalizer_middle";
  party.traffic_down = "equalizer_down";
  LoadScenario(party);

  ScenarioSpec birthday("birthday", "", "");
  birthday.full_random = true;
  LoadScenario(birthday);
}

static void ReportUnknownConfig(const std::string& line) {
  fprintf(stderr, "Unknown config line '%s'\n", line.c_str());
}

static ClientConfig ReadConfig(const std::string& app_path) {
  ClientConfig result;
  result.app_path = app_path;

  std::string full_config_path = app_path + "/config.txt";
  std::ifstream config_file(full_config_path);
  if (config_file.fail()) {
    fprintf(stderr, "Unable to read '%s'\n", full_config_path.c_str());
    return result;
  }

  std::string line;
  while (std::getline(config_file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    size_t equal_pos = line.find('=');
    if (equal_pos == std::string::npos) {
      fprintf(stderr, "Line without equals sign in config file\n");
      continue;
    }

    std::string name = trim(line.substr(0, equal_pos));
    std::string value = trim(line.substr(equal_pos + 1));

    if (name == "matrix") {
      if (value == "chinese") {
        result.is_matrix_chinese = true;
      } else if (value == "adafruit") {
        result.is_matrix_chinese = false;
      } else {
        ReportUnknownConfig(line);
      }
      continue;
    }

    if (name == "traffic_light_id") {
      result.traffic_light_id = std::stoi(value);
      continue;
    }

    if (name == "is_sequential") {
      result.is_sequential = (std::stoi(value) != 0);
      continue;
    }

    if (name == "first_scenario") {
      result.first_scenario = value;
      continue;
    }

    if (name == "red_green_light_sec") {
      result.red_green_light_ms = std::stoi(value) * 1000;
      continue;
    }

    if (name == "yellow_light_sec") {
      result.yellow_light_ms = std::stoi(value) * 1000;
      continue;
    }

    ReportUnknownConfig(line);
  }

  fprintf(stderr, "App path '%s', is_chinese=%d\n", result.app_path.c_str(),
          (int)result.is_matrix_chinese);
  return result;
}

//
//------------------------MAIN----------------------------------------
//
int main(int argc, const char* argv[]) {
  srand(time(nullptr));

  // Redirect stderr to stdout.
  dup2(1, 2);

  const char* app_path = ".";
  if (argc > 1) {
    app_path = argv[1];
  }

  char full_app_path[PATH_MAX + 1];
  if (!realpath(app_path, full_app_path)) {
    fprintf(stderr, "Unable to get real path for '%s'\n", app_path);
    return 1;
  }

  client_config = ReadConfig(full_app_path);

  if (argc > 2) {
    TryConnectUdp();
    Broadcast(argv[2], true, true);
    return 0;
  }

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
  matrix_options.brightness = 70;

  // Options to eliminate visual glitches and artifacts:
  matrix_options.pwm_bits = 4;
  matrix_options.pwm_lsb_nanoseconds = 300;
  // matrix_options.pwm_dither_bits = 2;
  rgb_matrix::RuntimeOptions runtime_opt;
  runtime_opt.gpio_slowdown = 4;

  if (client_config.is_matrix_chinese) {
    matrix_options.multiplexing = 2;
    matrix_options.led_rgb_sequence = "BGR";
  } else {
    matrix_options.led_rgb_sequence = "RGB";
  }

  // SetServerAddress("192.168.88.100", 1235);

  TryConnectUdp();

  SetRotations(std::vector<int>({-90, -90, -90, -90, -90, 90, 90, 90, 90, 90}));

  InitImages(client_config.app_path);

  LoadAllScenarios();

  matrix = CreateMatrixFromOptions(matrix_options, runtime_opt);
  if (matrix == NULL) {
    return 1;
  }

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
