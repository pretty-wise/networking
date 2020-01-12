#include <cstddef>
#include <cstdint>
#include <sys/socket.h>

int create_udp_addr(const char *hostname, uint16_t port,
                    sockaddr_storage *addr);

int open_socket(uint16_t *port);

size_t socket_receive(int handle, void *buffer, size_t nbytes,
                      struct sockaddr_storage *source, int *error);

size_t socket_send(int handle, struct sockaddr_storage *destination,
                   const void *buffer, size_t nbytes, int *error);