#pragma once
#include "simcommon/types.h"

struct sc_simulation;

sc_simulation *simclient_create();

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
};

int simclient_info(sc_simulation *sim, sc_info *info);