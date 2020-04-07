#pragma once
#include <stdint.h>

template <uint32_t TValue, int TNumBits> struct GetRequiredBitsHelper {
  enum { Value = GetRequiredBitsHelper<(TValue >> 1), TNumBits + 1>::Value };
};

template <int TNumBits> struct GetRequiredBitsHelper<0, TNumBits> {
  enum { Value = TNumBits };
};

template <uint32_t TValue> struct GetRequiredBits {
  enum { Value = GetRequiredBitsHelper<TValue, 0>::Value };
};

uint32_t bits_required(uint32_t min, uint32_t max);

class BitWriter {
public:
  BitWriter(uint8_t *buffer, uint32_t nbytes);

  void WriteBits(uint32_t value, uint32_t bits);

  uint32_t Flush();

  bool IsAvailable(uint32_t bits) const;
  uint64_t BitsAvailable() const;
  uint64_t BitsWritten() const;

private:
  uint32_t *m_buffer;
  uint32_t m_nwords;
  uint64_t m_scratch;
  uint32_t m_scratch_bits;
  uint32_t m_write_word;
  uint64_t m_bits_written;
};

class BitReader {
public:
  BitReader(const uint8_t *buffer, uint32_t nbytes);

  uint32_t ReadBits(int bits);

  bool IsAvailable(uint32_t bits) const;
  uint64_t BitsAvailable() const;
  uint64_t BitsRead() const;

private:
  const uint32_t *m_buffer;
  uint32_t m_nwords;
  uint64_t m_scratch;
  uint32_t m_scratch_bits;
  uint32_t m_read_word;
  uint64_t m_bits_read;
};

void TestBitReadWrite();

class ReadStream {
public:
  enum { IsWriting = 0 };
  enum { IsReading = 1 };

  ReadStream(const uint8_t *buffer, uint32_t nbytes);

  bool SerializeInteger(uint32_t &value, uint32_t min, uint32_t max);

private:
  BitReader m_reader;
  enum Error { None, OutOfMemory } m_error;
};

class WriteStream {
public:
  enum { IsWriting = 1 };
  enum { IsReading = 0 };

  WriteStream(uint8_t *buffer, uint32_t nbytes);

  bool SerializeInteger(uint32_t &value, uint32_t min, uint32_t max);

private:
  BitWriter m_writer;
  enum Error { None, Overflow } m_error;
};

#define serialize_int(stream, value, min, max)                                 \
  do {                                                                         \
    assert(min < max);                                                         \
    int32_t local;                                                             \
    if(Stream::IsWriting) {                                                    \
      assert(value >= min);                                                    \
      assert(value <= max);                                                    \
      local = value;                                                           \
    }                                                                          \
    if(!stream.SerializeInteger(value, min, max))                              \
      return false;                                                            \
    if(Stream::IsReading) {                                                    \
      value = local;                                                           \
      if(value < min || value > max)                                           \
        return false;                                                          \
    }                                                                          \
  } while(0)