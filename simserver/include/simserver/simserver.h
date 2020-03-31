#pragma once
#include "simcommon/types.h"

#define SIMSERVER_STATE_PEER_CONNECTED 2
#define SIMSERVER_STATE_PEER_DISCONNECTED 3
#define SIMSERVER_PEER_CAPACITY 16

struct ss_simulation;
struct entitymovement_t;

struct ss_config {
  uint32_t frame_duration;
  uint64_t start_time;
  frameid_t start_frame;
};

void simserver_make_default(ss_config *config);

ss_simulation *simserver_create(ss_config *config);

void simserver_destroy(ss_simulation *);

int simserver_start(ss_simulation *sim, ss_config *config);

void simserver_stop(ss_simulation *sim);

uint32_t simserver_write(uint16_t id, void *buffer, uint32_t nbytes,
                         simpeer_t *peer, ss_simulation *sim);

uint32_t simserver_read(uint16_t id, const void *buffer, uint32_t nbytes,
                        simpeer_t *peer, ss_simulation *sim);

void simserver_ack(uint16_t id, simpeer_t *peer, ss_simulation *sim);

void simserver_connection(uint32_t state, simpeer_t *peer, ss_simulation *sim);

void simserver_update(ss_simulation *sim);

struct ss_info {
  bool running;
  frameid_t head;
  uint32_t peer_count;
  simpeer_t *peer_id[SIMSERVER_PEER_CAPACITY];
  entityid_t remote_entity[SIMSERVER_PEER_CAPACITY];
  uint32_t input_buffer_size[SIMSERVER_PEER_CAPACITY];
  float *buffer_size_log[SIMSERVER_PEER_CAPACITY];
  uint32_t buffer_size_log_size[SIMSERVER_PEER_CAPACITY];
};

int simserver_info(ss_simulation *sim, ss_info *info);

entityid_t simserver_entity_create(ss_simulation *sim, simpeer_t *owner);
int simserver_entity_destroy(ss_simulation *sim, entityid_t entity);

int simserver_entity_movement(ss_simulation *sim, entityid_t **ids,
                              entitymovement_t **data, uint32_t *count);