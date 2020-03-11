#include "netcommon/reliability.h"
#include <algorithm>
#include <assert.h>

/*
send_head
send_buffer[kLogSize]

recv_head
recv_buffer[kLogSize]

send:
1. increate send head
2. add entry to send_buffer
  - send_buffer could override entries when going
    over kLogSize without receiving acks.
3. attach recv_head + bits based on recv_buffer

recv:
1. increase recv_head if packet is more recent
2. clear entries between old recv_head and new recv_head to avoid miss acks.
3. add entry to recv_buffer (regardless if more recent or not)
4. decode 32 acks and notify the application about not already acked packets.
*/

Reliability::Reliability() : m_rtt_log(200), m_smoothed_rtt_log(200) {
  Reset();
}

sequence_t Reliability::GenerateNewSequenceId(sequence_t *ack,
                                              sequence_bitmask_t *ack_bitmask) {
  sequence_t id = m_local_head;
  OutboundPacketInfo &info = SetSentPacketInfo(id);
  info.m_acked = false;
  info.m_send_time = get_time_ms();
  *ack = m_remote_head;
  *ack_bitmask = 0;

  sequence_bitmask_t mask = 1;
  for(sequence_t i = 0; i < 32; ++i) {
    sequence_t id = m_remote_head - i;
    if(m_recv_packets[id % kLogSize] == id) {
      *ack_bitmask |= mask;
    }
    mask <<= 1;
  }

  ++m_local_head;
  return id;
}

bool Reliability::IsStale(sequence_t sequence) {
  return IsLessRecent(sequence, m_remote_head - kLogSize);
}

sequence_bitmask_t Reliability::Ack(sequence_t sequence, sequence_t ack,
                                    sequence_bitmask_t ack_bitmask) {

  if(IsMoreRecent(sequence, m_remote_head)) {
    // clean entries here
    {
      // cleanup <from, to)
      int from = m_remote_head + 1;
      int to = sequence;
      if(to < from) {
        int sequence_range = std::numeric_limits<sequence_t>::max() + 1;
        to += sequence_range;
      }

      if(to - from >= kLogSize) {
        // clean all
        from = 0;
        to = kLogSize;
      }

      for(int i = from; i < to; ++i) {
        m_recv_packets[i % kLogSize] = (sequencelog_t)-1;
      }
    }

    m_remote_head = sequence;
    InboundPacketInfo &recvInfo = SetRecvPacketInfo(sequence);
    // todo(kstasik): store transmission info
  }

  sequence_bitmask_t toAckBitmask = 0;
  for(int i = 0; i < 32; ++i) {
    sequence_t seq = ack - i;
    bool is_acked = ack_bitmask & (1 << i);
    if(is_acked) {
      OutboundPacketInfo *sentInfo = FindSentPacketInfo(seq);
      if(sentInfo && !sentInfo->m_acked) {
        toAckBitmask |= (1 << i);

        sentInfo->m_acked = true;
        m_last_rtt = get_time_ms() - sentInfo->m_send_time;

        m_smoothed_rtt += ((float)m_last_rtt - m_smoothed_rtt) * 0.025f;
        m_rtt_log.PushBack((float)m_last_rtt);
        m_smoothed_rtt_log.PushBack((float)m_smoothed_rtt);
      }
    }
  }

  m_last_acked = ack;
  m_last_acked_bitmask = ack_bitmask;

  return toAckBitmask;
}

void Reliability::Reset() {
  m_local_head = kStartSequenceId;
  m_remote_head = kStartSequenceId - 1;
  m_last_acked = kStartSequenceId - 1;
  m_last_acked_bitmask = 0;
  memset(m_sent_packets, -1, kLogSize * sizeof(sequencelog_t));
  memset(m_recv_packets, -1, kLogSize * sizeof(sequencelog_t));

  m_rtt_log.Clear();
  m_smoothed_rtt_log.Clear();
  m_rtt_log.Resize(m_rtt_log.Capacity());
  m_smoothed_rtt_log.Resize(m_smoothed_rtt_log.Capacity());

  uint32_t initialRttValue = 30;
  m_last_rtt = initialRttValue;
  m_smoothed_rtt = initialRttValue;
}

Reliability::OutboundPacketInfo *
Reliability::FindSentPacketInfo(sequence_t id) {
  const uint32_t index = id % kLogSize;
  if(m_sent_packets[index] == id) {
    return &m_sent_data[index];
  }
  return nullptr;
}

Reliability::OutboundPacketInfo &Reliability::SetSentPacketInfo(sequence_t id) {
  const uint32_t index = id % kLogSize;
  m_sent_packets[index] = id;
  return m_sent_data[index];
}

Reliability::InboundPacketInfo &Reliability::SetRecvPacketInfo(sequence_t id) {
  const uint32_t index = id % kLogSize;
  m_recv_packets[index] = id;
  return m_recv_data[index];
}

static bool IsAcked(sequence_t id, sequence_t ack, sequence_bitmask_t bitmask) {
  for(sequence_t i = 0; i < 32; ++i) {
    if((bitmask & 1) != 0) {
      sequence_t acked = (sequence_t)(ack - i);
      if(acked == id)
        return true;
    }
    bitmask >>= 1;
  }
  return false;
}

void Reliability::Test() {
  int loopCount = std::max<int>(2 * std::numeric_limits<sequence_t>::max(),
                                2 * Reliability::kLogSize);
  // only outgoing packets
  {
    Reliability sender;
    bool first_sequence = true;

    sequence_t prev_id = Reliability::kStartSequenceId - 1;
    for(int i = 0; i < loopCount; ++i) {
      sequence_t ack;
      sequence_bitmask_t ack_bitmask;
      sequence_t id = sender.GenerateNewSequenceId(&ack, &ack_bitmask);

      assert(!first_sequence || id == kStartSequenceId);
      assert(ack == (sequence_t)(kStartSequenceId - 1));
      assert(ack_bitmask == 0);
      assert(IsMoreRecent(id, prev_id));

      prev_id = id;
      first_sequence = false;
    }
  }
  // 1 packet sent; 1 received
  {
    Reliability sender;
    Reliability receiver;

    assert(sender.m_local_head == kStartSequenceId);
    assert(receiver.m_local_head == kStartSequenceId);

    for(int i = 0; i < loopCount; ++i) {
      sequence_t sender_ack, receiver_ack;
      sequence_bitmask_t sender_ack_bitmask, receiver_ack_bitmask;

      sequence_t sender_outbound_id =
          sender.GenerateNewSequenceId(&receiver_ack, &receiver_ack_bitmask);
      assert(sender.m_local_head == (sequence_t)(sender_outbound_id + 1));

      bool processed = !receiver.IsStale(sender_outbound_id);
      if(processed)
        receiver.Ack(sender_outbound_id, receiver_ack, receiver_ack_bitmask);
      assert(processed);
      assert(receiver.m_remote_head == sender_outbound_id);

      sequence_t receiver_outbound_id =
          receiver.GenerateNewSequenceId(&sender_ack, &sender_ack_bitmask);
      assert(sender.m_local_head == (sequence_t)(receiver_outbound_id + 1));

      processed = !sender.IsStale(receiver_outbound_id);
      if(processed)
        sender.Ack(receiver_outbound_id, sender_ack, sender_ack_bitmask);

      assert(IsAcked(sender_outbound_id, sender_ack, sender_ack_bitmask));

      //("sender: %d, ack: %d, bmask: %d\n", sender_outbound_id, sender_ack,
      //       sender_ack_bitmask);
      for(int a = 0; a < 32; ++a) {
        sequence_t check_id = (sequence_t)(sender_ack - a);
        int num_expected_acks = i + 1;
        if(a < num_expected_acks) {
          assert(IsAcked(check_id, sender_ack, sender_ack_bitmask));
        } else {
          assert(!IsAcked(check_id, sender_ack, sender_ack_bitmask));
        }
      }

      assert(processed);
      assert(sender.m_remote_head == receiver_outbound_id);
    }
  }

  // drop packets
  {
    Reliability sender;
    Reliability receiver;
    for(int i = 0; i < loopCount; ++i) {
      sequence_t sender_ack, receiver_ack;
      sequence_bitmask_t sender_ack_bitmask, receiver_ack_bitmask;

      bool processed = false;

      sequence_t sender_outbound_id =
          sender.GenerateNewSequenceId(&receiver_ack, &receiver_ack_bitmask);

      if(sender_outbound_id % 2 == 0) {
        processed = !receiver.IsStale(sender_outbound_id);
        if(processed)
          receiver.Ack(sender_outbound_id, receiver_ack, receiver_ack_bitmask);
      }
      sequence_t receiver_outbound_id =
          receiver.GenerateNewSequenceId(&sender_ack, &sender_ack_bitmask);

      processed = !sender.IsStale(receiver_outbound_id);
      if(processed)
        sender.Ack(receiver_outbound_id, sender_ack, sender_ack_bitmask);

      // printf("sender: %d, ack: %d, bmask: %d\n", sender_outbound_id,
      // sender_ack,
      //       sender_ack_bitmask);

      for(int a = 0; a < 32; ++a) {
        sequence_t check_id = (sequence_t)(sender_ack - a);
        int num_expected_acks = i + 1;
        if(a < num_expected_acks && ((check_id % 2) == 0)) {
          assert(IsAcked(check_id, sender_ack, sender_ack_bitmask));
        } else {
          assert(!IsAcked(check_id, sender_ack, sender_ack_bitmask));
        }
      }
    }
  }
}
