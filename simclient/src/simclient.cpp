#include "simclient/simclient.h"
#include "simcommon/protocol.h"
#include <assert.h>

struct simclient_t {
  frameid_t remote_head;
};

struct sc_simulation {
  simclient_t *simulation;
  siminput_t m_last_input;
  void (*m_input_callback)(siminput_t *input);
};

sc_simulation *simclient_create(sc_config *config) {
  if(!config || !config->input_callback)
    return nullptr;

  sc_simulation *sim = new sc_simulation{};
  sim->m_input_callback = config->input_callback;
  return sim;
}

void simclient_destroy(sc_simulation *sim) { delete sim; }

uint32_t simclient_write(sc_simulation *sim, uint16_t id, void *buffer,
                         uint32_t nbytes) {
  if(!sim)
    return 0;

  assert(sizeof(CommandMessage) <= nbytes);

  auto *msg = (CommandMessage *)buffer;
  msg->m_type = MessageType::Command;
  msg->m_buttons = sim->m_last_input.m_buttons;
  return sizeof(CommandMessage);
}

uint32_t simclient_read(sc_simulation *sim, uint16_t id, const void *buffer,
                        uint32_t nbytes) {
  if(!sim)
    return 0;

  const auto *base = (const SimulationMessage *)buffer;
  if(base->m_type == MessageType::Update) {

    // todo(kstasik): read other data
    const auto *msg = (const UpdateMessage *)buffer;
    if(msg->m_frame_duration > 0) {
      if(!sim->simulation) {
        // create simulation
        sim->simulation = new simclient_t;
      }
      if(msg->m_frame_id > sim->simulation->remote_head) {
        sim->simulation->remote_head = msg->m_frame_id;
      } // else ignore out of order and duplicate packets
    } else {
      delete sim->simulation;
      sim->simulation = nullptr;
    }
    return 0;
  }

  return -1;
}

void simclient_ack(sc_simulation *sim, uint16_t id) {
  if(!sim)
    return;
}

void simclient_update(sc_simulation *sim) {
  if(!sim)
    return;

  sim->m_input_callback(&sim->m_last_input);
}

int simclient_info(sc_simulation *sim, sc_info *info) {
  if(!sim || !info)
    return -1;

  info->running = sim->simulation != nullptr;

  if(sim->simulation) {
    info->local_head = 0;
    info->remote_head = sim->simulation->remote_head;
  }
  return 0;
}