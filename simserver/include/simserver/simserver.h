#pragma once
#include "simcommon/types.h"

struct ss_simulation;

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
};

int simserver_info(ss_simulation *sim, ss_info *info);