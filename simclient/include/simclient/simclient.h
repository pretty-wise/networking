#pragma once
#include "simcommon/types.h"

struct sc_simulation;
struct entitymovement_t;

struct sc_config {
  void (*input_callback)(simcmd_t *input);
};

sc_simulation *simclient_create(sc_config *config);

void simclient_destroy(sc_simulation *);

uint32_t simclient_write(sc_simulation *sim, uint16_t id, void *buffer,
                         uint32_t nbytes);

uint32_t simclient_read(sc_simulation *sim, uint16_t id, const void *buffer,
                        uint32_t nbytes);

void simclient_ack(sc_simulation *sim, uint16_t id);

void simclient_update(sc_simulation *sim);

struct sc_info {
  bool running;
  frameid_t local_head;
  frameid_t remote_head;
  frameid_t acked_frame;
  int64_t prediction_offset;
  int64_t desired_offset;
  int64_t prediction_acceleration;

  float *offset_log;
  float *acceleration_log;
  uint32_t log_size;
};

int simclient_info(sc_simulation *sim, sc_info *info);

int simclient_entity_movement(sc_simulation *sim, entityid_t **ids,
                              entitymovement_t **data, uint32_t *count);