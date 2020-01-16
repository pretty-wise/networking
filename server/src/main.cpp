
#include "common/reliability.h"
#include "common/time.h"
#include "server.h"
#include <memory>
#include <signal.h>
#include <unistd.h>

bool g_running = true;
void term_handler(int signal) { g_running = false; }

Server *m_server;

int main(int argc, char *argv[]) {
  Reliability::Test();
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

  m_server = new Server(port, 16);

  while(g_running) {
    uint64_t frame_start_time = get_time_us();

    m_server->Update();

    uint64_t frame_end_time = get_time_us();
    const uint64_t desired_frame_time = 16000;
    uint64_t frame_time = frame_end_time - frame_start_time;
    if(frame_time < desired_frame_time) {
      usleep(desired_frame_time - frame_time);
    }
  }

  delete m_server;
  return 0;
}
