#include "netcommon/time.h"

#include <mach/mach.h>
#include <mach/mach_time.h>

double to_us_ratio() {
  mach_timebase_info_data_t info;
  (void)mach_timebase_info(&info);

  double ratio = (double)info.numer / (double)info.denom;

  return ratio;
}

double to_ms_ratio() { return to_us_ratio() * 0.000001; }

uint32_t get_time_ms() {
  uint64_t current = mach_absolute_time();

  static double ratio = to_ms_ratio();

  return (uint32_t)(current * ratio);
}

uint64_t get_time_us() {
  uint64_t current = mach_absolute_time();

  static double ratio = to_ms_ratio();

  return (uint64_t)(current * ratio);
}