#pragma once
#include "netcommon/sequence.h"
#include "utils/circularbuffer.h"
#include "utils/time.h"
#include <cstdint>
#include <string.h>

class Reliability {
public:
  static const sequence_t kStartSequenceId = 0;

  Reliability();

  struct OutboundPacketInfo {
    bool m_acked;
    uint32_t m_send_time;
  };

  struct InboundPacketInfo {
    bool m_nbytes;
  };

  // move m_local_head
  // store outbound packet information
  // return ack data based on last 32 inbound packets (m_recv_packets)
  sequence_t GenerateNewSequenceId(sequence_t *ack,
                                   sequence_bitmask_t *ack_bitmask);

  // if packet is older than m_remote_head - kLogSize
  // we discard it as we overriden its inbound info (m_recv_packets)
  // if packet is more recent than m_remote_head; update m_remote_head
  // clean inbound info (m_recv_packets) of potentially lost packets between
  // <m_remote_head + 1, sequence)
  // dispatch acks if not already dispatched.

  bool IsStale(sequence_t sequence);
  sequence_bitmask_t Ack(sequence_t sequence, sequence_t ack,
                         sequence_bitmask_t ack_bitmask);
  void Reset();

  static void Test();

  sequence_t GetLastSendId() const { return m_local_head - 1; }
  sequence_t GetLastRecvId() const { return m_remote_head; }
  sequence_t GetLastAckedId() const { return m_last_acked; }
  sequence_bitmask_t GetLastAckedIdBitmask() const {
    return m_last_acked_bitmask;
  }

private:
  typedef uint32_t sequencelog_t;
  static_assert(sizeof(sequence_t) < sizeof(sequencelog_t),
                "-1 needs to be represent invalid sequence_t value");

private:
  OutboundPacketInfo *FindSentPacketInfo(sequence_t id);
  OutboundPacketInfo &SetSentPacketInfo(sequence_t id);
  InboundPacketInfo &SetRecvPacketInfo(sequence_t id);

  sequence_t m_local_head;  // next id to be sent
  sequence_t m_remote_head; // last received id

  sequence_t m_last_acked;
  sequence_t m_last_acked_bitmask;

  static const uint32_t kLogSize = 256;
  sequencelog_t m_sent_packets[kLogSize];
  OutboundPacketInfo m_sent_data[kLogSize];

  sequencelog_t m_recv_packets[kLogSize];
  InboundPacketInfo m_recv_data[kLogSize];

public:
  CircularBuffer<float> m_rtt_log;
  CircularBuffer<float> m_smoothed_rtt_log;
  uint32_t m_last_rtt;
  float m_smoothed_rtt;
};
