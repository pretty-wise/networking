#pragma once
#include <cstdint>
#include <limits>

typedef uint16_t sequence_t;
typedef uint32_t sequence_bitmask_t;

inline bool IsMoreRecent(sequence_t a, sequence_t b) {
  return ((a > b) && (a - b <= std::numeric_limits<sequence_t>::max() / 2)) ||
         ((b > a) && (b - a > std::numeric_limits<sequence_t>::max() / 2));
}

inline bool IsLessRecent(sequence_t a, sequence_t b) {
  return IsMoreRecent(b, a);
}

inline size_t Distance(sequence_t a, sequence_t b) {
  // return b - a;
  return (b >= a) ? b - a
                  : (std::numeric_limits<sequence_t>::max() - a) + b + 1;
}