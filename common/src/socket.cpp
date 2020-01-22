#include "common/socket.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int create_udp_addr(const char *hostname, uint16_t port,
                    sockaddr_storage *address) {

  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = 0;
  hints.ai_protocol = IPPROTO_UDP;

  char service[8];
  sprintf(service, "%d", port);

  struct addrinfo *results;
  int err = getaddrinfo(hostname, service, &hints, &results);
  if(err) {
    return -1;
  }

  for(struct addrinfo *p = results; p != NULL; p = p->ai_next) {
    memcpy(address, p->ai_addr, sizeof(struct sockaddr));
    //*length = p->ai_addrlen;
    freeaddrinfo(results);
    return 0;
  }
  freeaddrinfo(results);
  return 0;
}

int sock_addr_cmp_addr(const struct sockaddr_storage *sa,
                       const struct sockaddr_storage *sb) {
  if(sa->ss_family != sb->ss_family)
    return (sa->ss_family - sb->ss_family);

  /*
   * With IPv6 address structures, assume a non-hostile implementation that
   * stores the address as a contiguous sequence of bits. Any holes in the
   * sequence would invalidate the use of memcmp().
   */
  if(sa->ss_family == AF_INET) {
    return ((sockaddr_in *)sa)->sin_addr.s_addr -
           ((sockaddr_in *)sb)->sin_addr.s_addr;
  } else if(sa->ss_family == AF_INET6) {
    return (memcmp((char *)((sockaddr_in6 *)sa), (char *)((sockaddr_in6 *)sb),
                   sizeof(sockaddr_in6)));
  }
  return -1;
}

/* sock_addr_cmp_port - compare ports for equality */

int sock_addr_cmp_port(const struct sockaddr_storage *sa,
                       const struct sockaddr_storage *sb) {
  if(sa->ss_family != sb->ss_family)
    return (sa->ss_family - sb->ss_family);

  if(sa->ss_family == AF_INET) {
    return ((sockaddr_in *)sa)->sin_port - ((sockaddr_in *)sb)->sin_port;
  } else if(sa->ss_family == AF_INET6) {
    return ((sockaddr_in6 *)sa)->sin6_port - ((sockaddr_in6 *)sb)->sin6_port;
  }
  return -1;
}
int sockaddr_compare(const sockaddr_storage *a, const sockaddr_storage *b) {
  return sock_addr_cmp_addr(a, b) == 0 && sock_addr_cmp_port(a, b) == 0 ? 0
                                                                        : -1;
}

int open_socket(uint16_t *port) {
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(*port);
  addr.sin_addr.s_addr = 0;

  int handle = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if(handle == -1) {
    return -1;
  }

  if(-1 == bind(handle, (struct sockaddr *)&addr, sizeof(addr))) {
    close(handle);
    return -1;
  }

  if(-1 == fcntl(handle, F_SETFL, O_NONBLOCK)) {
    close(handle);
    return -1;
  }

  struct sockaddr_in result;
  socklen_t result_length = sizeof(result);
  if(-1 == getsockname(handle, (struct sockaddr *)&result, &result_length)) {
    close(handle);
    return -1;
  }

  *port = ntohs(result.sin_port);
  return handle;
}

size_t socket_receive(int handle, void *buffer, size_t nbytes,
                      struct sockaddr_storage *source, int *error) {
  socklen_t addr_len = sizeof(sockaddr_storage);
  size_t received =
      recvfrom(handle, buffer, nbytes, 0, (sockaddr *)source, &addr_len);
  if(-1 == received) {
    if(errno == EWOULDBLOCK) {
      return 0;
    }
    *error = errno;
  }
  return received;
}

void close_socket(int socket) { close(socket); }

size_t socket_send(int handle, struct sockaddr_storage *destination,
                   const void *buffer, size_t nbytes, int *error) {
  size_t sent = sendto(handle, buffer, nbytes, 0,
                       (struct sockaddr *)destination, destination->ss_len);
  if(sent == -1) {
    *error = errno;
  }
  return sent;
}