#pragma once

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <stdint.h>

namespace rgb_matrix {
class FrameCanvas;
}

// Indicates for how long a frame is to be shown.
static constexpr uint32_t kHoldTimeMs = 125;

class Frame {
 public:
  static constexpr uint8_t kPixelWidth = 3;

  Frame() = default;
  Frame(const Frame& src) { *this = src; }
  Frame(Frame&& src);
  Frame(size_t width, size_t height, uint8_t r, uint8_t g, uint8_t b);
  ~Frame() { Clear(); }

  inline size_t width() const { return width_; }
  inline size_t height() const { return height_; }
  inline size_t stride() const { return stride_; }
  inline size_t data_size() const { return stride_ * height_; }
  inline const uint8_t* data() const { return data_; }
  inline uint8_t* data() { return data_; }

  void Clear();
  void SetSize(size_t width, size_t height);
  void FillColor(uint8_t r, uint8_t g, uint8_t b);

  Frame& operator=(const Frame& src);
  Frame& operator=(Frame&& src);

 private:
  size_t width_ = 0;
  size_t height_ = 0;
  size_t stride_ = 0;
  uint8_t* data_ = nullptr;
};

struct Animation {
  std::string name;
  std::vector<Frame> frames;
  size_t width = 0;
  size_t height = 0;
  size_t frame_count = 0;

  Animation(const std::string& name) : name(name) {}
};

struct AnimationState {
  const Animation* animation = nullptr;
  size_t cur_frame = 0;
  int rotation = 0;
  bool flip_v = false;
  bool flip_h = false;

  // If true, starts showing again right after it ends.
  // If false, freezes upon reaching the end.
  bool is_cyclic = false;

  size_t frame_count() const {
    return (animation ? animation->frame_count : 0);
  }

  operator bool() const { return (animation != nullptr); }

  AnimationState() = default;
  AnimationState(const Animation* animation) : animation(animation) {}
};

struct Collection {
  std::string name;
  std::vector<Animation> animations;

  Collection(const std::string& name) : name(name) {}

  const Animation* FindAnimation(const std::string& name) const;
};

Collection* FindCollection(const std::string& name);

void InitImages(const std::string& app_path);

void CreateIntBasedCollection(const std::string& name,
                              const std::vector<int>& sequence_ids);

AnimationState GetSolidBlack();
AnimationState GetSolidRed();
AnimationState GetSolidYellow();
AnimationState GetSolidGreen();

size_t GetMaxFrameCount(const std::vector<const Animation*>& animations);
size_t GetMaxFrameCount(const std::vector<AnimationState>& animations);

void SetRotations(const std::vector<int>& rotations);

bool RenderFrame(const std::vector<AnimationState*>& animations,
                 rgb_matrix::FrameCanvas* canvas);
