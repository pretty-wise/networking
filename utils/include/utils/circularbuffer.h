#pragma once
#include <assert.h>
#include <stdint.h>

template <class T> class CircularBuffer {
public:
  CircularBuffer(size_t num_elements);
  ~CircularBuffer();

  size_t PushBack(size_t count);
  size_t PopFront(size_t count);

  void PushBack(const T &v);

  T *Begin();
  const T *Begin() const;

  T *End();
  const T *End() const;

  size_t Size() const;
  size_t Capacity() const;
  void Resize(size_t);

  void Clear();

private:
  CircularBuffer(const CircularBuffer &);
  CircularBuffer &operator=(const CircularBuffer &);

  T *m_buffer;
  T *m_read;
  T *m_write;
  size_t m_capacity;
};

template <class T> CircularBuffer<T>::CircularBuffer(size_t num_elements) {
  m_buffer = new T[2 * num_elements];
  m_capacity = num_elements;
  m_read = m_buffer;
  m_write = m_buffer;
}

template <class T> CircularBuffer<T>::~CircularBuffer() { delete[] m_buffer; }

template <class T> size_t CircularBuffer<T>::PushBack(size_t count) {
  if(count > Capacity()) {
    count = Capacity();
  }

  size_t elements_left = m_capacity - Size();
  if(count > elements_left) {
    PopFront(count - elements_left);
  }

  m_write += count;
  assert(m_write < m_buffer + 2 * m_capacity);
  return count;
}
template <class T> size_t CircularBuffer<T>::PopFront(size_t count) {
  size_t size = Size();
  if(count > size) {
    count = size;
  }
  m_read += count;
  if(m_read - m_buffer >= m_capacity) {
    m_read -= m_capacity;
    m_write -= m_capacity;
  }
  assert(m_read >= m_buffer);
  assert(m_write >= m_buffer);
  assert(m_write < m_buffer + (2 * m_capacity));
  assert(m_read < m_buffer + (2 * m_capacity));
  return count;
}

template <class T> T *CircularBuffer<T>::Begin() { return m_read; }
template <class T> const T *CircularBuffer<T>::Begin() const { return m_read; }

template <class T> T *CircularBuffer<T>::End() { return m_write; }
template <class T> const T *CircularBuffer<T>::End() const { return m_write; }

template <class T> size_t CircularBuffer<T>::Size() const {
  assert(m_write >= m_read);
  return m_write - m_read;
}
template <class T> size_t CircularBuffer<T>::Capacity() const {
  return m_capacity;
}

template <class T> void CircularBuffer<T>::Resize(size_t size) {
  PushBack(size);
}

template <class T> void CircularBuffer<T>::PushBack(const T &v) {
  T *e = End();
  assert(e >= m_buffer);
  assert(e < m_buffer + (m_capacity * 2));
  *e = v;
  if(e < m_buffer + m_capacity) {
    assert(e + m_capacity >= m_buffer);
    assert(e + m_capacity < m_buffer + (m_capacity * 2));
    *(e + m_capacity) = v;
  } else {
    assert(e - m_capacity >= m_buffer);
    assert(e - m_capacity < m_buffer + (m_capacity * 2));
    *(e - m_capacity) = v;
  }

  PushBack((size_t)1);
}

template <class T> void CircularBuffer<T>::Clear() {
  m_read = m_buffer;
  m_write = m_buffer;
}