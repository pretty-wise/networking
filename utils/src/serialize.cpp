#include "utils/serialize.h"
#include <assert.h>

static uint32_t popcount(uint32_t x) {
  const uint32_t a = x - ((x >> 1) & 0x55555555);
  const uint32_t b = (((a >> 2) & 0x33333333) + (a & 0x33333333));
  const uint32_t c = (((b >> 4) + b) & 0x0f0f0f0f);
  const uint32_t d = c + (c >> 8);
  const uint32_t e = d + (d >> 16);
  const uint32_t result = e & 0x0000003f;
  return result;
}

static uint32_t log2(uint32_t x) {
  const uint32_t a = x | (x >> 1);
  const uint32_t b = a | (a >> 2);
  const uint32_t c = b | (b >> 4);
  const uint32_t d = c | (c >> 8);
  const uint32_t e = d | (d >> 16);
  const uint32_t f = e >> 1;
  return popcount(f);
}

uint32_t bits_required(uint32_t min, uint32_t max) {
  return min == max ? 0 : log2(max - min) + 1;
}

//
// BitWriter
//

BitWriter::BitWriter(uint8_t *buffer, uint32_t nbytes)
    : m_buffer((uint32_t *)buffer), m_nwords(nbytes / 4), m_scratch(0),
      m_scratch_bits(0), m_write_word(0), m_bits_written(0) {
  assert(nbytes % 4 == 0);
}

void BitWriter::WriteBits(uint32_t value, uint32_t bits) {
  assert(bits > 0);
  assert(bits <= 32);
  assert(BitsAvailable() >= bits);

  value &= ((uint64_t(1) << bits) - 1);

  m_scratch |= (uint64_t(value) << m_scratch_bits);
  m_scratch_bits += bits;
  m_bits_written += bits;
  if(m_scratch_bits >= 32) {
    assert(m_write_word < m_nwords);
    // this need to take care of endian swapping if big endian platform needs to
    // be supported
    m_buffer[m_write_word] = m_scratch & 0xffffffff;
    m_write_word++;
    m_scratch_bits -= 32;
    m_scratch >>= 32;
  }
}

uint32_t BitWriter::Flush() {
  if(m_scratch_bits > 0) {
    m_buffer[m_write_word] = m_scratch & 0xffffffff;
    m_write_word++;
    m_scratch_bits = 0;
    m_scratch = 0;
  }
  return (m_write_word - 1) * 4;
}

uint64_t BitWriter::BitsAvailable() const {
  return (m_nwords * 32) - m_bits_written;
}

bool BitWriter::IsAvailable(uint32_t bits) const {
  return BitsAvailable() >= bits;
}

uint64_t BitWriter::BitsWritten() const { return m_bits_written; }

//
// BitReader
//

BitReader::BitReader(const uint8_t *buffer, uint32_t nbytes)
    : m_buffer((const uint32_t *)buffer), m_nwords(nbytes / 4), m_scratch(0),
      m_scratch_bits(0), m_read_word(0), m_bits_read(0) {
  assert(nbytes % 4 == 0);
}

uint32_t BitReader::ReadBits(int bits) {
  assert(bits > 0);
  assert(bits <= 32);

  if(m_scratch_bits < bits) {
    // handle endianness here if big endian needs to be supported
    m_scratch |= uint64_t(m_buffer[m_read_word]) << m_scratch_bits;
    m_scratch_bits += 32;
    m_read_word++;
  }

  uint32_t output = m_scratch & ((uint64_t(1) << bits) - 1);
  m_scratch >>= bits;
  m_scratch_bits -= bits;
  m_bits_read += bits;
  return output;
}

bool BitReader::IsAvailable(uint32_t bits) const {
  return BitsAvailable() >= bits;
}

uint64_t BitReader::BitsAvailable() const {
  return (m_nwords * 32) - m_bits_read;
}

uint64_t BitReader::BitsRead() const { return m_bits_read; }

void TestBitReadWrite() {
  uint32_t nbytes = 16;
  uint8_t buffer[nbytes];

  BitWriter writer(buffer, nbytes);

  const uint32_t firstValue = 4;
  const uint32_t secondValue = -1;
  writer.WriteBits(firstValue, GetRequiredBits<firstValue>::Value);

  assert(writer.BitsAvailable() ==
         nbytes * 8 - GetRequiredBits<firstValue>::Value);
  assert(writer.BitsWritten() == GetRequiredBits<firstValue>::Value);

  writer.WriteBits(secondValue, GetRequiredBits<secondValue>::Value);

  assert(writer.BitsAvailable() == nbytes * 8 -
                                       GetRequiredBits<firstValue>::Value -
                                       GetRequiredBits<secondValue>::Value);

  writer.Flush();

  assert(writer.BitsWritten() == GetRequiredBits<firstValue>::Value +
                                     GetRequiredBits<secondValue>::Value);

  BitReader reader(buffer, nbytes);

  uint32_t firstResult = reader.ReadBits(GetRequiredBits<firstValue>::Value);
  uint32_t secondResult = reader.ReadBits(GetRequiredBits<secondValue>::Value);
  assert(firstValue == firstResult);
  assert(secondValue == secondResult);

  assert(writer.BitsWritten() == reader.BitsRead());
  assert(writer.BitsAvailable() == reader.BitsAvailable());
}

//
// ReadStream
//

ReadStream::ReadStream(const uint8_t *buffer, uint32_t nbytes)
    : m_reader(buffer, nbytes), m_error(Error::None) {}

bool ReadStream::SerializeInteger(uint32_t &value, uint32_t min, uint32_t max) {
  assert(min < max);

  const uint32_t num_bits = bits_required(min, max);
  if(!m_reader.IsAvailable(num_bits)) {
    m_error = Error::OutOfMemory;
    return false;
  }

  value = m_reader.ReadBits(num_bits);
  return true;
}

//
// WriteStream
//

WriteStream::WriteStream(uint8_t *buffer, uint32_t nbytes)
    : m_writer(buffer, nbytes), m_error(Error::None) {}

bool WriteStream::SerializeInteger(uint32_t &value, uint32_t min,
                                   uint32_t max) {
  assert(min < max);
  assert(value >= min);
  assert(value <= max);

  uint32_t num_bits = bits_required(min, max);

  if(!m_writer.IsAvailable(num_bits)) {
    m_error = Error::Overflow;
    return false;
  }

  m_writer.WriteBits(value, num_bits);
  return true;
}