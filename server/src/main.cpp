#include "netserver/netserver.h"
#include <memory>
#include <signal.h>
#include <unistd.h>

#include <mach/mach.h>
#include <mach/mach_time.h>

static double to_us_ratio() {
  mach_timebase_info_data_t info;
  (void)mach_timebase_info(&info);

  double ratio = (double)info.numer / (double)info.denom;

  return ratio;
}

static double to_ms_ratio() { return to_us_ratio() * 0.000001; }

static uint32_t get_time_ms() {
  uint64_t current = mach_absolute_time();

  static double ratio = to_ms_ratio();

  return (uint32_t)(current * ratio);
}

static uint64_t get_time_us() {
  uint64_t current = mach_absolute_time();

  static double ratio = to_ms_ratio();

  return (uint64_t)(current * ratio);
}

bool g_running = true;
void term_handler(int signal) { g_running = false; }

void *g_server = nullptr;

int main(int argc, char *argv[]) {
  if(SIG_ERR == signal(SIGINT, term_handler)) {
    return -1;
  }
  if(SIG_ERR == signal(SIGTERM, term_handler)) {
    return -1;
  }

  uint16_t port = 0;

  if(argc > 1) {
    port = atoi((const char *)argv[1]);
  }

  g_server = netserver_create(&port, 16);

  while(g_running) {
    uint64_t frame_start_time = get_time_us();

    netserver_update(g_server);

    uint64_t frame_end_time = get_time_us();
    const uint64_t desired_frame_time = 16000;
    uint64_t frame_time = frame_end_time - frame_start_time;
    if(frame_time < desired_frame_time) {
      usleep(desired_frame_time - frame_time);
    }
  }

  netserver_destroy(g_server);
  return 0;
}
