#pragma once

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <stdint.h>

namespace rgb_matrix {
class FrameCanvas;
class MemStreamIO;
}

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

  Animation(const std::string& name) : name(name) {}
};

struct Collection {
  std::string name;
  std::vector<Animation> animations;

  Collection(const std::string& name) : name(name) {}

  const Animation* FindAnimation(const std::string& name) const;
};

Collection* FindCollection(const std::string& name);

void InitImages();

void CreateIntBasedCollection(const std::string& name,
                              const std::vector<int>& sequence_ids);

void RenderAnimations(const std::vector<const Animation*>& animations,
                      rgb_matrix::FrameCanvas* canvas,
                      rgb_matrix::MemStreamIO* output);
