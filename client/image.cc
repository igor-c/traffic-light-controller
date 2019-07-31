#include "image.h"

#include <algorithm>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include <Magick++.h>

#include "led-matrix.h"

static const Frame* empty_frame = nullptr;
static std::vector<Collection> all_collections;

Frame::Frame(Frame&& src) {
  *this = std::move(src);
}

Frame::Frame(size_t width, size_t height, uint8_t r, uint8_t g, uint8_t b) {
  SetSize(width, height);
  FillColor(r, g, b);
}

Frame& Frame::operator=(const Frame& src) {
  SetSize(src.width_, src.height_);
  std::memcpy(data_, src.data_, data_size());
  return *this;
}

Frame& Frame::operator=(Frame&& src) {
  width_ = src.width_;
  height_ = src.height_;
  stride_ = src.stride_;
  data_ = src.data_;
  src.data_ = nullptr;
  src.Clear();
  return *this;
}

void Frame::Clear() {
  width_ = 0;
  height_ = 0;
  stride_ = 0;
  free(data_);
  data_ = nullptr;
}

void Frame::SetSize(size_t width, size_t height) {
  Clear();
  width_ = width;
  height_ = height;
  stride_ = width_ * kPixelWidth;
  data_ = (uint8_t*)malloc(data_size());
}

void Frame::FillColor(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t* c = data_;
  for (size_t x = 0; x < width_; ++x) {
    c[0] = r;
    c[1] = g;
    c[2] = b;
    c += kPixelWidth;
  }

  c = data_ + stride_;
  for (size_t y = 1; y < height_; ++y) {
    std::memcpy(c, data_, stride_);
    c += stride_;
  }
}

const Animation* Collection::FindAnimation(const std::string& name) const {
  for (const Animation& animation : animations) {
    if (animation.name == name)
      return &animation;
  }
  return nullptr;
}

static bool StringEndsWith(const std::string& str, const std::string& suffix) {
  return (str.size() >= suffix.size() &&
          0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix));
}

static bool StringStartsWith(const std::string& str,
                             const std::string& prefix) {
  return (str.size() >= prefix.size() &&
          0 == str.compare(0, prefix.size(), prefix));
}

static std::vector<std::string> GetDirList(const std::string& path,
                                           const std::string& suffix) {
  std::vector<std::string> result;
  DIR* dir = opendir(path.c_str());
  if (!dir) {
    printf("Unable to list '%s'\n", path.c_str());
    return result;
  }

  while (true) {
    struct dirent* entry = readdir(dir);
    if (!entry)
      break;

    std::string name(entry->d_name);
    if (name == "." || name == "..")
      continue;

    if (entry->d_type == DT_REG) {
      if (suffix.empty() || StringEndsWith(name, suffix)) {
        result.push_back(path + "/" + name);
      }
      continue;
    }

    if (entry->d_type == DT_DIR) {
      std::vector<std::string> files = GetDirList(path + "/" + name, suffix);
      result.insert(result.end(), files.begin(), files.end());
    }
  }

  closedir(dir);
  return result;
}

static inline void RotateCoordinates(size_t src_x,
                                     size_t src_y,
                                     size_t* dst_x,
                                     size_t* dst_y,
                                     int rotation,
                                     bool flip_v,
                                     bool flip_h) {
  if (rotation == 90) {
    *dst_y = src_x;
    *dst_x = 31 - src_y;
  } else if (rotation == 180) {
    *dst_x = 31 - src_x;
    *dst_y = 31 - src_y;
  } else if (rotation == 270) {
    *dst_y = 31 - src_x;
    *dst_x = src_y;
  } else {
    *dst_x = src_x;
    *dst_y = src_y;
  }

  if (flip_v)
    *dst_y = 31 - *dst_y;

  if (flip_h)
    *dst_x = 31 - *dst_x;
}

static void ConvertToFrame(const Magick::Image& src, Frame* dst) {
  // Somehow, all our images have humans heads on the left,
  // and so need to be rotated 90CW. We do it below.

  size_t src_width = src.columns();
  size_t src_height = src.rows();
  dst->SetSize(src_height, src_width);  // Swap width/height due to rotation.

  const Magick::PixelPacket* pixels =
      src.getConstPixels(0, 0, src_width, src_height);
  // src.writePixels(Magick::QuantumType::RGBAQuantum, (unsigned char*)pixels);

  const Magick::PixelPacket* src_c = pixels;
  uint8_t* dst_pixels = dst->data();
  for (size_t src_y = 0; src_y < src_height; ++src_y) {
    for (size_t src_x = 0; src_x < src_width; ++src_x) {
      size_t dst_x, dst_y;
      RotateCoordinates(src_x, src_y, &dst_x, &dst_y, 90, false, false);
      uint8_t* dst_c =
          dst_pixels + dst_y * dst->stride() + dst_x * Frame::kPixelWidth;
      if (src_c->opacity < 256) {
        dst_c[0] = ScaleQuantumToChar(src_c->red);
        dst_c[1] = ScaleQuantumToChar(src_c->green);
        dst_c[2] = ScaleQuantumToChar(src_c->blue);
      }
      src_c += 1;
    }
  }
}

static void LoadAnimation(Collection* collection,
                          const std::string& name,
                          const std::string& path) {
  std::vector<Magick::Image> src_frames;
  if (!path.empty()) {
    try {
      Magick::readImages(&src_frames, path);
    } catch (Magick::ErrorFileOpen& e) {
      printf("Failed to load image '%s'\n", path.c_str());
    }
  }

  if (src_frames.size() > 1) {
    // Unpack an animated GIF into a series of same-size frames.
    std::vector<Magick::Image> unpacked_frames;
    Magick::coalesceImages(&unpacked_frames, src_frames.begin(),
                           src_frames.end());
    src_frames = unpacked_frames;
  }

  if (!src_frames.empty() &&
      (src_frames[0].columns() != 32 || src_frames[0].rows() != 32)) {
    printf("Discarding '%s' because of incorrect size %dx%d\n", path.c_str(),
           src_frames[0].columns(), src_frames[0].rows());
    src_frames.clear();
  }

  collection->animations.emplace_back(name);
  Animation& animation = collection->animations.back();

  std::vector<Frame>& dst_frames = animation.frames;
  for (const Magick::Image& src : src_frames) {
    dst_frames.emplace_back();
    ConvertToFrame(src, &dst_frames.back());
  }

  if (dst_frames.empty())
    dst_frames.push_back(*empty_frame);

  animation.width = dst_frames[0].width();
  animation.height = dst_frames[0].height();

  animation.frame_count = dst_frames.size();

  printf("Loaded '%s/%s' from '%s', frames=%d, size=%dx%d\n",
         collection->name.c_str(), name.c_str(), path.c_str(),
         animation.frame_count, animation.width, animation.height);
}

Collection* FindCollection(const std::string& name) {
  for (auto& item : all_collections) {
    if (item.name == name)
      return &item;
  }
  return nullptr;
}

static Collection* FindOrCreateCollection(const std::string& name) {
  Collection* collection = FindCollection(name);
  if (collection)
    return collection;

  all_collections.emplace_back(name);
  return &all_collections.back();
}

static void LoadAllImageFiles() {
  std::vector<std::string> paths = GetDirList("images", ".gif");
  for (const auto& path : paths) {
    std::string name = path;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    name = name.substr(7, path.size() - 7 - 4);

    size_t p = name.find('/');
    if (p == std::string::npos) {
      printf("No directory found for image '%s'\n", path.c_str());
      continue;
    }

    std::string collection_name = name.substr(0, p);
    name = name.substr(p + 1);

    if (name.find('/') != std::string::npos) {
      printf("Double-nested directories not supported: '%s'\n", path.c_str());
      continue;
    }

    if (StringStartsWith(name, collection_name + "_")) {
      name = name.substr(collection_name.size() + 1);
    }

    Collection* collection = FindOrCreateCollection(collection_name);
    LoadAnimation(collection, name, path);
  }
}

void InitImages() {
  Magick::InitializeMagick("");

  empty_frame = new Frame(32, 32, 0, 0, 0);

  LoadAllImageFiles();
}

void CreateIntBasedCollection(const std::string& name,
                              const std::vector<int>& sequence_ids) {
  Collection* collection = FindOrCreateCollection(name);

  std::vector<Animation> image_sequences;
  for (int i = 0; i < 10; ++i) {
    int sequence_id = sequence_ids[i];

    char image_name[256];
    std::snprintf(image_name, sizeof(image_name), "%d", sequence_id);

    char image_path[256];
    if (sequence_id) {
      std::snprintf(image_path, sizeof(image_path), "gif/%d.gif", sequence_id);
    } else {
      image_path[0] = 0;
    }

    LoadAnimation(collection, image_name, image_path);
  }

  printf("Finished creating int-based collection '%s'\n", name.c_str());
}

size_t GetMaxFrameCount(const std::vector<const Animation*>& animations) {
  size_t result = 0;
  for (const Animation* animation : animations) {
    if (animation->frame_count > result)
      result = animation->frame_count;
  }
  return result;
}

size_t GetMaxFrameCount(const std::vector<AnimationState>& animations) {
  size_t result = 0;
  for (const AnimationState& animation : animations) {
    if (animation.animation->frame_count > result)
      result = animation.animation->frame_count;
  }
  return result;
}

static bool RenderFrame(const AnimationState& animation_state,
                        rgb_matrix::FrameCanvas* canvas,
                        size_t x_offset) {
  const Animation* animation = animation_state.animation;
  const Frame& frame = (animation_state.cur_frame < animation->frame_count
                            ? animation->frames[animation_state.cur_frame]
                            : animation->frames.back());

  int rotation = animation_state.rotation;
  if (rotation < 0)
    rotation += 360;

  if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
    printf("Unexpected rotation of %d, animation '%s'\n",
           animation_state.rotation, animation->name.c_str());
    return false;
  }

  bool flip_v = false;
  bool flip_h = false;

  for (size_t src_y = 0; src_y < 32; ++src_y) {
    const uint8_t* c = frame.data() + src_y * frame.stride();
    for (size_t src_x = 0; src_x < 32; ++src_x) {
      size_t dst_x, dst_y;
      RotateCoordinates(src_x, src_y, &dst_x, &dst_y, rotation, flip_v, flip_h);
      canvas->SetPixel(dst_x + x_offset, dst_y, c[0], c[1], c[2]);
      c += Frame::kPixelWidth;
    }
  }

  return true;
}

bool RenderFrame(const std::vector<AnimationState>& animations,
                 rgb_matrix::FrameCanvas* canvas) {
  int needed_width = animations.size() * 32;
  if (canvas->height() != 32 || canvas->width() < needed_width ||
      canvas->width() % 32 != 0) {
    printf("Unexpected canvas size of %dx%d, animations=%d\n", canvas->width(),
           canvas->height(), animations.size());
    return false;
  }

  size_t position_count = (size_t)canvas->width() / 32;
  for (size_t position = 0; position < animations.size(); ++position) {
    // Rendering starts from the end of the image, reverse positions.
    size_t x_offset = (position_count - position - 1) * 32;
    const AnimationState& animation_state = animations[position];
    if (!RenderFrame(animation_state, canvas, x_offset)) {
      return false;
    }
  }

  return true;
}
