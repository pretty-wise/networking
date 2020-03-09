#include "netcommon/netsimulator.h"
#include "netcommon/socket.h"
#include "netcommon/time.h"
#include <cstring>
#include <stdlib.h>

static const uint32_t MAX_PENDING_PACKETS = 256;

struct PacketEntry {
  sockaddr_storage m_src;
  sockaddr_storage m_dst;
  uint8_t *m_buffer = nullptr;
  uint32_t m_nbytes;
  uint32_t m_deliverTime;
};

struct netsimulator {
  uint32_t m_delayMs;
  uint32_t m_jitterMs;
  float m_packetDropRatio = 0.f;
  float m_packetDuplicationRatio = 0.f;
  uint32_t m_time = 0;
  PacketEntry m_pending[MAX_PENDING_PACKETS];
  uint32_t m_pendingIndex = 0;
};

void netsimulator_default_config(netsimulator_config *config) {
  config->delayMs = 30;
  config->jitterMs = 0;
  config->dropRatio = 0.f;
  config->duplicateRatio = 0.f;
}

netsimulator *netsimulator_create(const netsimulator_config *config) {
  netsimulator *sim = new netsimulator;
  sim->m_delayMs = config->delayMs;
  sim->m_jitterMs = config->jitterMs;
  sim->m_packetDropRatio = config->dropRatio;
  sim->m_packetDuplicationRatio = config->duplicateRatio;
  sim->m_pendingIndex = 0;
  for(int i = 0; i < MAX_PENDING_PACKETS; ++i) {
    sim->m_pending[i].m_buffer = nullptr;
  }
  return sim;
}

void netsimulator_configure(netsimulator *sim,
                            const netsimulator_config *config) {
  sim->m_delayMs = config->delayMs;
  sim->m_jitterMs = config->jitterMs;
  sim->m_packetDropRatio = config->dropRatio;
  sim->m_packetDuplicationRatio = config->duplicateRatio;
}

void netsimulator_destroy(netsimulator *context) {
  for(int i = 0; i < MAX_PENDING_PACKETS; ++i) {
    free(context->m_pending[i].m_buffer);
  }
  delete context;
}

static void queue(netsimulator *sim, void *buffer, uint32_t nbytes,
                  const sockaddr_storage &src, const sockaddr_storage &dst,
                  uint32_t delayMs) {
  PacketEntry &entry = sim->m_pending[sim->m_pendingIndex];
  free(entry.m_buffer);
  entry.m_buffer = (uint8_t *)malloc(nbytes);
  memcpy(entry.m_buffer, buffer, nbytes);
  entry.m_nbytes = nbytes;
  entry.m_deliverTime = sim->m_time + delayMs;
  memcpy(&entry.m_src, &src, sizeof(sockaddr_storage));
  memcpy(&entry.m_dst, &dst, sizeof(sockaddr_storage));
  sim->m_pendingIndex = (sim->m_pendingIndex + 1) % MAX_PENDING_PACKETS;
}

void netsimulator_send(netsimulator *sim, void *buffer, uint32_t nbytes,
                       const sockaddr_storage &src,
                       const sockaddr_storage &dst) {
  if(((float)rand() / (float)RAND_MAX) < sim->m_packetDropRatio)
    return;

  uint32_t delayMs =
      sim->m_delayMs +
      (2.f * ((float)rand() / (float)RAND_MAX) - 1.f) * sim->m_jitterMs;

  queue(sim, buffer, nbytes, src, dst, delayMs);

  if(((float)rand() / (float)RAND_MAX) < sim->m_packetDuplicationRatio) {
    queue(sim, buffer, nbytes, src, dst, delayMs);
  }
}

uint32_t netsimulator_recv(netsimulator *sim, const sockaddr_storage &dst,
                           uint8_t **buffer, uint32_t *nbytes,
                           sockaddr_storage *src, uint32_t max_received) {
  uint32_t num_received = 0;
  for(uint32_t i = 0; i < MAX_PENDING_PACKETS; ++i) {
    PacketEntry &entry = sim->m_pending[i];
    if(num_received >= max_received)
      continue;

    if(!entry.m_buffer)
      continue;

    if(0 != sockaddr_compare(&entry.m_dst, &dst))
      continue;

    if(entry.m_deliverTime < sim->m_time) {
      buffer[num_received] = entry.m_buffer;
      entry.m_buffer = nullptr;
      nbytes[num_received] = entry.m_nbytes;
      src[num_received] = entry.m_src;
      ++num_received;
    }
  }
  return num_received;
}

void netsimulator_update(netsimulator *sim) { sim->m_time = get_time_ms(); }
