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

Reliability::Reliability() { Reset(); }

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

bool Reliability::OnReceived(sequence_t sequence, sequence_t ack,
                             sequence_bitmask_t ack_bitmask, const void *buffer,
                             size_t nbytes, read_packet read_func,
                             on_ack ack_func, on_nack nack_func) {
  bool too_old = IsLessRecent(sequence, m_remote_head - kLogSize);
  if(too_old)
    return false;

  int read_error = read_func(sequence, buffer, nbytes);
  if(read_error <= 0)
    return false;

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

  m_last_acked = ack;
  m_last_acked_bitmask = ack_bitmask;

  for(sequence_t i = 0; i < 32; ++i) {
    if((ack_bitmask & 1) != 0) {
      sequence_t id = ack - i;
      OutboundPacketInfo *sentInfo = FindSentPacketInfo(id);
      if(sentInfo && !sentInfo->m_acked) {
        sentInfo->m_acked = true;
        uint32_t rtt = get_time_ms() - sentInfo->m_send_time;
        // todo(kstasik): use rtt
        ack_func(id);
      }
    }
    ack_bitmask >>= 1;
  }

  // out of order and duplicate packets will be processed.
  return true;
}

void Reliability::Reset() {
  m_local_head = kStartSequenceId;
  m_remote_head = kStartSequenceId - 1;
  m_last_acked = kStartSequenceId - 1;
  m_last_acked_bitmask = 0;
  memset(m_sent_packets, -1, kLogSize * sizeof(sequencelog_t));
  memset(m_recv_packets, -1, kLogSize * sizeof(sequencelog_t));
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

static int test_read_func(sequence_t id, const void *buffer, size_t nbytes) {
  return 1;
}

static void test_ack_func(sequence_t id) {}
static void test_nack_func(sequence_t id) {}

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

      sequence_t sender_outboud_id =
          sender.GenerateNewSequenceId(&receiver_ack, &receiver_ack_bitmask);
      assert(sender.m_local_head == (sequence_t)(sender_outboud_id + 1));

      bool processed = receiver.OnReceived(
          sender_outboud_id, receiver_ack, receiver_ack_bitmask, nullptr, 0,
          test_read_func, test_ack_func, test_nack_func);
      assert(processed);
      assert(receiver.m_remote_head == sender_outboud_id);

      sequence_t receiver_outbound_id =
          receiver.GenerateNewSequenceId(&sender_ack, &sender_ack_bitmask);
      assert(sender.m_local_head == (sequence_t)(receiver_outbound_id + 1));

      processed = sender.OnReceived(
          receiver_outbound_id, sender_ack, sender_ack_bitmask, nullptr, 0,
          test_read_func, test_ack_func, test_nack_func);

      assert(IsAcked(sender_outboud_id, sender_ack, sender_ack_bitmask));

      //("sender: %d, ack: %d, bmask: %d\n", sender_outboud_id, sender_ack,
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

      sequence_t sender_outboud_id =
          sender.GenerateNewSequenceId(&receiver_ack, &receiver_ack_bitmask);

      if(sender_outboud_id % 2 == 0) {
        processed = receiver.OnReceived(
            sender_outboud_id, receiver_ack, receiver_ack_bitmask, nullptr, 0,
            test_read_func, test_ack_func, test_nack_func);
      }
      sequence_t receiver_outbound_id =
          receiver.GenerateNewSequenceId(&sender_ack, &sender_ack_bitmask);

      processed = sender.OnReceived(
          receiver_outbound_id, sender_ack, sender_ack_bitmask, nullptr, 0,
          test_read_func, test_ack_func, test_nack_func);

      // printf("sender: %d, ack: %d, bmask: %d\n", sender_outboud_id,
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