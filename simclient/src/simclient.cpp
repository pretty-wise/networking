#include "simclient/simclient.h"
#include "simcommon/protocol.h"
#include "simcommon/script.h"
#include "utils/circularbuffer.h"
#include "utils/serialize.h"
#include "utils/time.h"
#include <assert.h>
#include <cstring>
#include <math.h>
#include <stdio.h>

struct entitydata_t {
  // id of the last frame the movement changed on the server
  frameid_t m_frame_movement_confirmed;
  // id of the last frame the script changed on the server
  frameid_t m_frame_script_confirmed;
  // id of the frame the server sent the script update on. might be
  // different from m_frame_script_confirmed since things might have not changed
  frameid_t m_last_script_sample;
  // id of the last predicted frame
  frameid_t m_predicted;
  // id of frame when last prediction error happened. used for debuging
  frameid_t m_last_error;

  // data below needs to be extended to contain historical values
  // for local entity
  entitymovement_t m_movement_confirmed;
  entitymovement_t m_movement_predicted;
  entityscriptmgr_t m_script_confirmed;
  entityscriptmgr_t m_script_predicted;
};

struct clientsim_t {
  // the id of the last locally predicted frame.
  frameid_t m_local_head;

  entityid_t m_local_entity;

  entityid_t m_entity_id[SIMSERVER_ENTITY_CAPACITY];
  entitydata_t m_entity_data[SIMSERVER_ENTITY_CAPACITY];
  uint32_t m_entity_count;
};

static uint32_t find_entity(clientsim_t *sim, entityid_t id) {
  if(id == 0)
    return -1;
  for(uint32_t i = 0; i < SIMSERVER_ENTITY_CAPACITY; ++i) {
    if(sim->m_entity_id[i] == id)
      return i;
  }
  return -1;
}

static uint32_t add_entity(clientsim_t *sim, entityid_t id) {
  assert(id != 0);

  for(uint32_t i = 0; i < SIMSERVER_ENTITY_CAPACITY; ++i) {
    if(sim->m_entity_id[i] == 0) {
      sim->m_entity_id[i] = id;
      sim->m_entity_data[i] = {};

      // here we should run prediction from the frame the entity was created to
      // the m_local_head
      sim->m_entity_data[i].m_predicted = sim->m_local_head;

      ++sim->m_entity_count;
      return i;
    }
  }
  return -1;
}

static void step_client_simulation(clientsim_t *sim, siminput_t &input) {
  frameid_t frame = (sim->m_local_head += 1);

  uint32_t entity_idx = find_entity(sim, sim->m_local_entity);
  if(entity_idx != -1) {
    auto &entity_data = sim->m_entity_data[entity_idx];
    assert(entity_data.m_predicted == frame - 1);

    entitymovement_t prev_movement = entity_data.m_movement_predicted;
    step_movement(entity_data.m_movement_predicted, prev_movement, input);

    entityscriptmgr_t prev_script = entity_data.m_script_predicted;
    step_script(entity_data.m_script_predicted, prev_script, input);
    entity_data.m_predicted = frame;
  }
}

struct sc_simulation {
  sc_simulation()
      : m_offset_log(128), m_acceleration_log(128), m_cmd_log(128) {}

  clientsim_t *m_simulation = nullptr;

  CircularBuffer<simcmd_t> m_cmd_log; // last entry is of sim->m_local_head
  void (*m_input_callback)(simcmd_t *input);

  uint64_t m_last_update_time = 0;
  uint64_t m_frame_duration = 0;
  uint64_t m_remote_sim_time_accumulator = 0;
  frameid_t m_remote_head = 0;
  int64_t m_predition_offset = 0;
  int64_t m_acceleration = 0;
  uint64_t m_local_sim_time_accumulator = 0;
  int64_t m_desired_predition_offset = 0;
  frameid_t m_acked_remote_frame = 0;

  CircularBuffer<float> m_offset_log;
  CircularBuffer<float> m_acceleration_log;
};

static siminput_t get_input(sc_simulation *sim, frameid_t frame) {
  frameid_t head_frame = sim->m_simulation->m_local_head;
  assert(frame <= head_frame);
  assert(head_frame - (frame - 1) < sim->m_cmd_log.Size());

  auto last_cmd_it = sim->m_cmd_log.End() - 1;
  auto frame_cmd_it = last_cmd_it - (head_frame - frame);
  siminput_t input = {*frame_cmd_it, *(frame_cmd_it - 1)};
  return input;
}

static void start_simulation(sc_simulation *sim, uint64_t start_time,
                             uint64_t frame_duration, frameid_t start_frame) {
  assert(frame_duration > 0);

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

  sim->m_simulation = new clientsim_t{};
  sim->m_simulation->m_local_head = sim->m_remote_head;
  // fill the buffer with empty commands as if nothing was pressed to simplify
  // the algorithm
  for(uint32_t i = 0; i < 128; ++i)
    sim->m_cmd_log.PushBack(simcmd_t{});
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
  msg->m_frame_id = 0;
  msg->m_buttons = 0;
  if(sim->m_simulation) {
    msg->m_frame_id = sim->m_simulation->m_local_head;
    assert(sim->m_cmd_log.Size() > 0);
    simcmd_t last_cmd = *(sim->m_cmd_log.End() - 1);
    msg->m_buttons = last_cmd.m_buttons;
  }
  return sizeof(CommandMessage);
}

uint32_t simclient_read(sc_simulation *sim, uint16_t id, const void *buffer,
                        uint32_t nbytes) {
  if(!sim)
    return 0;

  const auto *base = (const SimulationMessage *)buffer;
  if(base->m_type != MessageType::Update)
    return -1;

  const auto *msg = (const UpdateMessage *)buffer;

  if(msg->m_frame_duration == 0) {
    stop_simulation(sim);
    return 0;
  }

  if(!sim->m_simulation) {
    start_simulation(sim, msg->m_start_time, msg->m_frame_duration,
                     msg->m_start_frame);
  }

  if(msg->m_frame_id <= sim->m_acked_remote_frame) {
    // ignore out of order and duplicate packets
    return 0;
  }

  sim->m_acked_remote_frame = msg->m_frame_id;
  sim->m_simulation->m_local_entity = msg->m_local_entity;

  bool predict_movement[SIMSERVER_ENTITY_CAPACITY] = {};
  bool predict_script[SIMSERVER_ENTITY_CAPACITY] = {};

  // read movement update
  for(uint32_t i = 0; i < msg->m_entity_movement_count; ++i) {
    auto *entity_update =
        (MovementUpdate *)((uint8_t *)buffer + sizeof(UpdateMessage) +
                           i * sizeof(MovementUpdate));

    uint32_t eidx = find_entity(sim->m_simulation, entity_update->m_id);
    if(eidx == -1)
      continue; // entity not yet discovered

    predict_movement[eidx] = true;

    auto &entity_data = sim->m_simulation->m_entity_data[eidx];
    entity_data.m_movement_confirmed = entity_update->m_movement;
    entity_data.m_frame_movement_confirmed = msg->m_frame_id;
  }

  // read script updates
  if(msg->m_script_update_count > 0) {
    auto *script_update =
        (ScriptUpdate *)((uint8_t *)buffer + sizeof(UpdateMessage) +
                         msg->m_entity_movement_count * sizeof(MovementUpdate));
    uint8_t *payload_buffer = (uint8_t *)script_update + sizeof(ScriptUpdate);

    for(uint32_t i = 0; i < msg->m_script_update_count; ++i) {
      uint32_t eidx = find_entity(sim->m_simulation, script_update->m_id);
      if(eidx == -1) {
        assert(script_update->m_create == true);
        eidx = add_entity(sim->m_simulation, script_update->m_id);
      }

      // entity limit reached. this could happen if we
      // have stale entities that we were not notified
      // about destruction of just yet. this case needs
      // to be supported.nsd
      // note: server should guarantee that this does not happen as we
      // must process scriptpacket_t because one cannot be nacked.
      assert(eidx != -1);

      if(eidx == -1)
        return 0;

      auto &entity_data = sim->m_simulation->m_entity_data[eidx];

      // read scriptpacket
      if(script_update->m_payload_size > 0) {
        assert(payload_buffer + script_update->m_payload_size <=
               (uint8_t *)buffer + nbytes);

        BitReader reader(payload_buffer, script_update->m_payload_size);

        bool is_local = reader.ReadBits(1) == 1;
        frameid_t start_frame = reader.ReadBits(32);
        frameid_t end_frame = reader.ReadBits(32);
        frameid_t confirmed = entity_data.m_frame_script_confirmed;
        frameid_t predicted = entity_data.m_predicted;

        // | confirmed | start_frame | ... | end frame | ... | predicted |
        // rollback from <predicted to confirmed)
        // apply from <start_frame to end_frame>
        // predict from (end_frame to predicted>

        if(start_frame != 0 &&
           start_frame != entity_data.m_frame_script_confirmed) {
          // ignore packet as there is some in-between packet missing
          // todo(kstasik): we need to at least log this
        } else {
          entity_data.m_last_script_sample = msg->m_frame_id;

          if(start_frame == 0) {
            // full update received
            entity_data.m_frame_script_confirmed = end_frame;
            entity_data.m_script_confirmed = {};

          } else if(start_frame == entity_data.m_frame_script_confirmed) {
            // todo(kstasik): think how we should handle frames that the
            // server sent twice because of a false-ack received from the
            // client. delta update received
            entity_data.m_frame_script_confirmed = end_frame;
          } else {
            assert(false); // should not happen
          }

          predict_script[eidx] = true;

          // apply script changes (full update or delta)
          {
            uint32_t script_update_count = reader.ReadBits(
                GetRequiredBits<SIMSERVER_SCRIPT_CAPACITY>::Value);
            for(uint32_t i = 0; i < script_update_count; ++i) {
              uint8_t lifetime_flags =
                  reader.ReadBits(2); // todo(kstasik): create scripts.

              // script creation
              if((lifetime_flags & script_create_flag) != 0) {
                scriptid_t id = reader.ReadBits(32);
                scriptguid_t guid = reader.ReadBits(32);
                printf("client script created: 0x%x 0x%x\n", id, guid);
                uint32_t idx =
                    insert_script(&entity_data.m_script_confirmed, id, guid);
                assert(idx != -1);
              }

              // script termination
              if((lifetime_flags & script_destroy_flag) != 0) {
                scriptid_t id = reader.ReadBits(32);
                scriptguid_t guid = reader.ReadBits(32);
                bool result =
                    remove_script(&entity_data.m_script_confirmed, guid);
                assert(result);
                printf("client script destroyed: 0x%x 0x%x\n", id, guid);
              }
            }
          }
        }
      }

      script_update =
          (ScriptUpdate *)(payload_buffer + script_update->m_payload_size);
      payload_buffer = (uint8_t *)script_update + sizeof(ScriptUpdate);
    }
  }

  // resimulate from m_last_script_sample to m_predicted and compare with
  // the current prediction
  {
    for(uint32_t eidx = 0; eidx < SIMSERVER_SCRIPT_CAPACITY; ++eidx) {
      auto &entity_data = sim->m_simulation->m_entity_data[eidx];
      if(!predict_script[eidx] && !predict_movement[eidx]) {
        continue;
      }

      entityscriptmgr_t prev_script_prediction = entity_data.m_script_predicted;
      entitymovement_t prev_movement_prediction =
          entity_data.m_movement_predicted;

      entity_data.m_movement_predicted = entity_data.m_movement_confirmed;

      if(predict_script[eidx])
        entity_data.m_script_predicted = entity_data.m_script_confirmed;

      //
      // prediction
      //
      if(predict_script[eidx]) {
        frameid_t step_frame = entity_data.m_last_script_sample + 1;
        while(step_frame <= entity_data.m_predicted) {
          siminput_t input = get_input(sim, step_frame);

          if(step_frame > entity_data.m_frame_movement_confirmed) {
            entitymovement_t prev_movement = entity_data.m_movement_predicted;
            step_movement(entity_data.m_movement_predicted, prev_movement,
                          input);
          }

          entityscriptmgr_t prev_script = entity_data.m_script_predicted;
          step_script(entity_data.m_script_predicted, prev_script, input);

          ++step_frame;
        }
      } else {
        // movement only prediction
        frameid_t step_frame = entity_data.m_frame_movement_confirmed + 1;
        while(step_frame <= entity_data.m_predicted) {
          siminput_t input = get_input(sim, step_frame);

          entitymovement_t prev_movement = entity_data.m_movement_predicted;
          step_movement(entity_data.m_movement_predicted, prev_movement, input);

          ++step_frame;
        }
      }
      //
      // /prediction
      //

      if(0 != memcmp(&prev_movement_prediction,
                     &entity_data.m_movement_predicted,
                     sizeof(entitymovement_t))) {
        entity_data.m_last_error = msg->m_frame_id;
      }
      if(predict_script[eidx] &&
         0 != memcmp(&prev_script_prediction, &entity_data.m_script_predicted,
                     sizeof(entityscriptmgr_t))) {
        entity_data.m_last_error = msg->m_frame_id;
      }
    }
  }

  // adjustment of prediction offset based on server's feedback.
  // this is a very simple implementation that can be easily improved.
  // if server reports 0 cmds we extend prediction offset by delta.
  // if server reports > 1 cmds we shrink prediction offset by delta.
  const uint32_t delta = 1000;
  if(msg->m_cmdbuffer_size == 0) {
    sim->m_desired_predition_offset += delta;
  } else if(msg->m_cmdbuffer_size > 1) {
    sim->m_desired_predition_offset -= delta;
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
      simcmd_t cmd;
      sim->m_input_callback(&cmd);

      sim->m_local_sim_time_accumulator += dt;

      while(true) {
        uint64_t frame_duration = sim->m_frame_duration + sim->m_acceleration;
        assert((int64_t)sim->m_frame_duration + sim->m_acceleration > 0);

        if(sim->m_local_sim_time_accumulator < frame_duration)
          break;

        assert(sim->m_cmd_log.Size() > 0);

        siminput_t input = {cmd, *(sim->m_cmd_log.End() - 1)};
        step_client_simulation(sim->m_simulation, input);
        sim->m_cmd_log.PushBack(cmd);

        sim->m_offset_log.PushBack((float)sim->m_predition_offset);
        sim->m_acceleration_log.PushBack((float)sim->m_acceleration);

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
    info->local_head = sim->m_simulation->m_local_head;
    info->remote_head = sim->m_remote_head;
    info->acked_frame = sim->m_acked_remote_frame;

    info->prediction_offset = sim->m_predition_offset;
    info->desired_offset = sim->m_desired_predition_offset;
    info->prediction_acceleration = sim->m_acceleration;

    assert(sim->m_offset_log.Size() == sim->m_acceleration_log.Size());
    info->acceleration_log = sim->m_acceleration_log.Begin();
    info->offset_log = sim->m_offset_log.Begin();
    info->log_size = sim->m_offset_log.Size();
  }
  return 0;
}

int simclient_entity_movement(sc_simulation *sim, entityid_t **ids,
                              entityinfo_t **data, uint32_t *count) {
  *count = 0;
  if(!sim->m_simulation)
    return -1;

  // this is a temporary solution to only return movement data.
  // the api needs to be improved and probably returned more details than just
  // movement
  static entityinfo_t transfer_buffer[SIMSERVER_ENTITY_CAPACITY];

  *count = sim->m_simulation->m_entity_count;
  if(*count > 0) {
    *ids = sim->m_simulation->m_entity_id;
    for(uint32_t i = 0; i < sim->m_simulation->m_entity_count; ++i) {
      auto &entity_data = sim->m_simulation->m_entity_data[i];
      transfer_buffer[i].movement_predicted = entity_data.m_movement_predicted;
      transfer_buffer[i].movement_confirmed = entity_data.m_movement_confirmed;
      transfer_buffer[i].confirmed = entity_data.m_frame_movement_confirmed;
      transfer_buffer[i].predicted = entity_data.m_predicted;
      transfer_buffer[i].last_error = entity_data.m_last_error;
    }
    *data = transfer_buffer;
  }
  return 0;
}

int simclient_entity_script(sc_simulation *sim, sc_script_info *info,
                            entityid_t entity) {
  if(!sim->m_simulation)
    return -1;

  uint32_t entity_idx = find_entity(sim->m_simulation, entity);
  if(entity_idx == -1)
    return -2;

  auto &entity_data = sim->m_simulation->m_entity_data[entity_idx];
  info->count = 0;
  for(uint32_t i = 0; i < SIMSERVER_SCRIPT_CAPACITY; ++i) {
    auto &script = entity_data.m_script_predicted.m_script[i];
    if(script.m_guid != 0) {
      info->guid[info->count] = script.m_guid;
      info->id[info->count] = script.m_id;
      ++info->count;
    }
  }

  info->last_changed = entity_data.m_frame_script_confirmed;
  info->last_sampled = entity_data.m_last_script_sample;
  return 0;
}
