#pragma once
#include <stdint.h>
#include <sys/socket.h>

extern "C" {

struct netsimulator;

struct netsimulator_config {
  uint32_t delayMs;
  uint32_t jitterMs;
  float dropRatio;
  float duplicateRatio;
};

void netsimulator_default_config(netsimulator_config *config);

netsimulator *netsimulator_create(const netsimulator_config *config);
void netsimulator_destroy(netsimulator *);
void netsimulator_send(netsimulator *sim, void *buffer, uint32_t nbytes,
                       const sockaddr_storage &src,
                       const sockaddr_storage &dst);
uint32_t netsimulator_recv(netsimulator *sim, const sockaddr_storage &dst,
                           uint8_t **buffer, uint32_t *nbytes,
                           sockaddr_storage *src, uint32_t max_received);
void netsimulator_update(netsimulator *sim);
void netsimulator_configure(netsimulator *sim,
                            const netsimulator_config *config);
}
