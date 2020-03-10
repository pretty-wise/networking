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

struct ns_server *g_server = nullptr;

static void packet_func(uint16_t id, void *user_data) {}

static int send_func(uint16_t id, void *buffer, uint32_t nbytes) { return 0; }

int recv_func(uint16_t id, const void *buffer, uint32_t nbytes) { return 0; }

void state_func(uint32_t state, ns_endpoint *e, void *user_data) {}

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

  ns_config config;
  config.port = port;
  config.num_endpoints = 16;
  config.packet_callback = packet_func;
  config.send_callback = send_func;
  config.recv_callback = recv_func;
  config.state_callback = state_func;
  config.user_data = nullptr;

  g_server = netserver_create(&config);

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
