#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <locale>
#include <string>
#include <vector>

#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

inline std::string ltrim(std::string s) {
  s.erase(s.begin(),
          std::find_if(s.begin(), s.end(),
                       std::not1(std::ptr_fun<int, int>(std::isspace))));
  return s;
}

inline std::string rtrim(std::string s) {
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       std::not1(std::ptr_fun<int, int>(std::isspace)))
              .base(),
          s.end());
  return s;
}

inline std::string trim(std::string s) {
  return rtrim(ltrim(s));
}

inline std::string JoinPath(const std::string& p1, const std::string& p2) {
  if (p1.empty())
    return p2;

  if (p2.empty())
    return p1;

  return p1 + "/" + p2;
}

inline uint64_t CurrentTimeMillis() {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

inline void SleepMillis(uint64_t milli_seconds) {
  if (milli_seconds <= 0)
    return;
  struct timespec ts;
  ts.tv_sec = milli_seconds / 1000;
  ts.tv_nsec = (milli_seconds % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

inline size_t GetRandom(size_t min, size_t max) {
  return min + rand() / (RAND_MAX / (max - min + 1) + 1);
}

inline bool IsRandomPercentile(size_t percent) {
  if (percent > 100)
    percent = 100;

  size_t r = GetRandom(0, 99);
  return (r < percent);
}

template <typename T>
inline const T* PickNextRandom(const std::vector<T>& list, const T* previous) {
  if (list.empty())
    return nullptr;

  if (list.size() == 1)
    return &list[0];

  // Try a few times to make sure there's no repeat.
  size_t next_idx = 0;
  for (int i = 0; i < 5; ++i) {
    next_idx = GetRandom(0, list.size() - 1);
    if (&list[next_idx] != previous)
      break;
  }

  return &list[next_idx];
}

template <typename T>
inline const T* PickNextSequential(const std::vector<T>& list,
                                   const T* previous) {
  if (list.empty())
    return nullptr;

  size_t pos;
  for (pos = 0; pos < list.size(); ++pos) {
    if (&list[pos] == previous)
      break;
  }

  pos++;
  if (pos >= list.size())
    pos = 0;

  return &list[pos];
}
