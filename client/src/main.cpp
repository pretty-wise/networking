#include "common/socket.h"
#include "common/time.h"
#include <cstdio>
#include <cstdlib>
#include <signal.h>
#include <unistd.h>

bool g_running = true;

void term_handler(int signal) { g_running = false; }

int main(int argc, char *argv[]) {
  if(argc < 2) {
    return -1;
  }

  int dst_port = atoi((const char *)argv[1]);

  sockaddr_storage destination;
  if(-1 == create_udp_addr("127.0.0.1", dst_port, &destination)) {
    return -1;
  }

  uint16_t src_port = 0;
  int socket = open_socket(&src_port);
  if(-1 == socket) {
    return -1;
  }

  printf("src: %d, dst: %d\n", src_port, dst_port);

  size_t nbytes = 256;
  char buffer[nbytes];
  int error = 0;

  while(g_running) {
    uint64_t frame_start_time = get_time_us();

    size_t sent =
        socket_send(socket, &destination, (const void *)buffer, nbytes, &error);
    printf("sent %lubytes. error: %d\n", sent, error);
    if(sent == -1) {
      return -2;
    }

    uint64_t frame_end_time = get_time_us();
    const uint64_t desired_frame_time = 16000;
    uint64_t frame_time = frame_end_time - frame_start_time;
    if(frame_time < desired_frame_time) {
      usleep(desired_frame_time - frame_time);
    }
  }

  return 0;
}