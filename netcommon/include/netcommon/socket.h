#pragma once
#include <cstddef>
#include <cstdint>
#include <sys/socket.h>

int create_udp_addr(const char *hostname, uint16_t port,
                    sockaddr_storage *addr);

int sockaddr_compare(const sockaddr_storage *a, const sockaddr_storage *b);

int open_socket(uint16_t *port);
void close_socket(int);

size_t socket_receive(int handle, void *buffer, size_t nbytes,
                      struct sockaddr_storage *source, int *error);

size_t socket_send(int handle, const struct sockaddr_storage *destination,
                   const void *buffer, size_t nbytes, int *error);