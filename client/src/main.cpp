#include "client.h"
#include "common/time.h"
#include <cstdlib>
#include <signal.h>
#include <unistd.h>

bool g_running = true;

void term_handler(int signal) { g_running = false; }

int main(int argc, char *argv[]) {
  if(SIG_ERR == signal(SIGINT, term_handler)) {
    return -1;
  }
  if(SIG_ERR == signal(SIGTERM, term_handler)) {
    return -1;
  }

  if(argc < 2) {
    return -1;
  }

  int dst_port = atoi((const char *)argv[1]);

  Client *client = new Client("127.0.0.1", dst_port);

  while(g_running) {
    uint64_t frame_start_time = get_time_us();

    client->Update();

    g_running = !client->IsDisconnected();

    uint64_t frame_end_time = get_time_us();
    const uint64_t desired_frame_time = 16000;
    uint64_t frame_time = frame_end_time - frame_start_time;
    if(frame_time < desired_frame_time) {
      usleep(desired_frame_time - frame_time);
    }
  }

  delete client;
  return 0;
}