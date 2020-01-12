
#include "common/socket.h"
#include "common/time.h"
#include <cstdio>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
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

  unsigned short port = 0;
  int socket = open_socket(&port);
  if(socket == -1) {
    return -1;
  }
  printf("handle=%d, port=%d\n", socket, port);

  size_t nbytes = 1024;
  char buffer[nbytes];
  sockaddr_storage source;
  int error;

  while(g_running) {
    uint64_t frame_start_time = get_time_us();

    size_t received = socket_receive(socket, buffer, nbytes, &source, &error);
    if(received > 0) {
      char host[1024];
      char service[20];
      getnameinfo((const sockaddr *)&source, source.ss_len, host, sizeof host,
                  service, sizeof service, 0);
      printf("received %lubytes from %s:%s\n", received, host, service);
    }

    uint64_t frame_end_time = get_time_us();
    const uint64_t desired_frame_time = 16000;
    uint64_t frame_time = frame_end_time - frame_start_time;
    if(frame_time < desired_frame_time) {
      usleep(desired_frame_time - frame_time);
    }
  }
  close(socket);

  return 0;
}
