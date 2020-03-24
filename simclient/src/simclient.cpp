#include "simclient/simclient.h"
#include "simcommon/protocol.h"
#include "utils/time.h"
#include <assert.h>
#include <math.h>

struct simclient_t {
  frameid_t m_head;
};

void step(simclient_t *sim, siminput_t &input) { sim->m_head += 1; }

struct sc_simulation {
  simclient_t *m_simulation;
  siminput_t m_last_input;
  void (*m_input_callback)(siminput_t *input);

  uint64_t m_last_update_time;
  uint64_t m_frame_duration;
  uint64_t m_remote_sim_time_accumulator;

  frameid_t m_remote_head;

  int64_t m_predition_offset;
  int64_t m_acceleration;

  uint64_t m_local_sim_time_accumulator;

  int64_t m_desired_predition_offset;

  frameid_t m_acked_remote_frame;
};

static void start_simulation(sc_simulation *sim, uint64_t start_time,
                             uint64_t frame_duration, frameid_t start_frame) {
  // todo(kstasik): m_remote_head calculations does not include
  // time synchronisation for now. it needs to be added to accurately deprict
  // remote_head on the client
  uint64_t now = get_time_us();
  uint64_t since_simulation_start = now - start_time;

  sim->m_last_update_time = now;
  sim->m_frame_duration = frame_duration;

  uint64_t integral = (uint64_t)(since_simulation_start / frame_duration);
  sim->m_remote_sim_time_accumulator =
      (uint64_t)fmodf((float)since_simulation_start, (float)frame_duration);
  sim->m_remote_head = start_frame + (frameid_t)integral;

  // todo(kstasik): initialize to something based on synchronized time
  sim->m_predition_offset = 0;
  sim->m_acceleration = 0;

  // for now, hardcoded to 100ms
  sim->m_desired_predition_offset = 100000;

  // todo(kstasik): is this the right value to initialize?
  sim->m_acked_remote_frame = 0;

  sim->m_simulation = new simclient_t;
  sim->m_simulation->m_head = sim->m_remote_head;
  sim->m_local_sim_time_accumulator = sim->m_remote_sim_time_accumulator;
}

static void stop_simulation(sc_simulation *sim) {
  delete sim->m_simulation;
  sim->m_simulation = nullptr;

  sim->m_acked_remote_frame = 0;
  sim->m_frame_duration = 0;
  sim->m_last_update_time = 0;
  sim->m_remote_head = 0;
  sim->m_remote_sim_time_accumulator = 0;

  sim->m_predition_offset = 0;
  sim->m_desired_predition_offset = 0;
  sim->m_acceleration = 0;
}

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
      if(!sim->m_simulation) {
        start_simulation(sim, msg->m_start_time, msg->m_frame_duration,
                         msg->m_start_frame);
      }
      if(msg->m_frame_id > sim->m_acked_remote_frame) {
        sim->m_acked_remote_frame = msg->m_frame_id;
      } // else ignore out of order and duplicate packets
    } else {
      stop_simulation(sim);
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

  if(sim->m_simulation) {
    uint64_t now = get_time_us();
    uint64_t dt = now - sim->m_last_update_time;

    // update server simulation frame
    {
      sim->m_remote_sim_time_accumulator += dt;

      uint64_t integral = (uint64_t)(sim->m_remote_sim_time_accumulator /
                                     sim->m_frame_duration);
      sim->m_remote_sim_time_accumulator =
          (uint64_t)fmodf((float)sim->m_remote_sim_time_accumulator,
                          (float)sim->m_frame_duration);
      sim->m_remote_head += (frameid_t)integral;

      // the code above will accumulate error
      // the other solution is:
      // sim->m_remote_head = m_startFrameId + (now -
      // m_simulationStartTimeSec) / sim->m_frame_duration;
    }

    // update offset and acceleration
    {
      int64_t delta = 1000; // todo(kstasik): what is the best delta?
      int64_t diff = sim->m_desired_predition_offset - sim->m_predition_offset;
      if(abs(diff) < delta) {
        sim->m_acceleration = 0;
      } else {
        sim->m_acceleration = -delta * (diff > 0 ? 1 : -1);
      }
    }

    // step local simulation
    {
      sim->m_input_callback(&sim->m_last_input);

      sim->m_local_sim_time_accumulator += dt;

      while(true) {
        uint64_t frame_duration = sim->m_frame_duration + sim->m_acceleration;
        assert((int64_t)sim->m_frame_duration + sim->m_acceleration > 0);

        if(sim->m_local_sim_time_accumulator < frame_duration)
          break;

        step(sim->m_simulation, sim->m_last_input);

        // m_predictionLog.Put((float)m_predictionOffset);
        // m_accelerationLog.Put((float)m_acceleration);

        sim->m_local_sim_time_accumulator -= frame_duration;
        sim->m_predition_offset -= sim->m_acceleration;
      }
    }

    sim->m_last_update_time = now;
  }
}

int simclient_info(sc_simulation *sim, sc_info *info) {
  if(!sim || !info)
    return -1;

  info->running = sim->m_simulation != nullptr;

  if(sim->m_simulation) {
    info->local_head = sim->m_simulation->m_head;
    info->remote_head = sim->m_remote_head;
    info->acked_frame = sim->m_acked_remote_frame;

    info->prediction_offset = sim->m_predition_offset;
    info->desired_offset = sim->m_desired_predition_offset;
    info->prediction_acceleration = sim->m_acceleration;
  }
  return 0;
}
