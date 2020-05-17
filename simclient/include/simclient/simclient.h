#pragma once
#include "simcommon/simulation.h"
#include "simcommon/types.h"

struct sc_simulation;

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

struct entityinfo_t {
  entitymovement_t movement_predicted;
  entitymovement_t movement_confirmed;
  frameid_t confirmed;
  frameid_t predicted;
  frameid_t last_error;
};

int simclient_entity_movement(sc_simulation *sim, entityid_t **ids,
                              entityinfo_t **data, uint32_t *count);

struct sc_script_info {
  scriptid_t id[SIMSERVER_SCRIPT_CAPACITY];
  scriptguid_t guid[SIMSERVER_SCRIPT_CAPACITY];
  uint32_t count;
  frameid_t last_changed;
  frameid_t last_sampled;
};

int simclient_entity_script(sc_simulation *sim, sc_script_info *info,
                            entityid_t entity);