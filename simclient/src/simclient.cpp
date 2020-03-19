#include "simclient/simclient.h"
#include "simcommon/protocol.h"

struct sc_simulation {
  frameid_t remote_head;
};

sc_simulation *simclient_create() {
  sc_simulation *sim = new sc_simulation{};
  return sim;
}

void simclient_destroy(sc_simulation *sim) { delete sim; }

uint32_t simclient_write(sc_simulation *sim, uint16_t id, void *buffer,
                         uint32_t nbytes) {
  if(!sim)
    return 0;
  return 0;
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
      sim->remote_head = msg->m_frame_id;
    } else {
      sim->remote_head = 0;
    }
    return sizeof(UpdateMessage);
  }

  return 0;
}

void simclient_ack(sc_simulation *sim, uint16_t id) {
  if(!sim)
    return;
}

void simclient_update(sc_simulation *sim) {
  if(!sim)
    return;
}

int simclient_info(sc_simulation *sim, sc_info *info) {
  if(!sim || !info)
    return -1;

  info->local_head = 0;
  info->remote_head = sim->remote_head;
  return 0;
}