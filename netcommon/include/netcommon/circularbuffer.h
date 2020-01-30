#pragma once
#include <assert.h>
#include <stdint.h>

class CircularBufferBase {
protected:
  void *Allocate(size_t *size);
  void Deallocate(void *buffer, size_t size);
};

template <class T> class CircularBuffer : protected CircularBufferBase {
public:
  CircularBuffer(size_t num_elements);
  ~CircularBuffer();

  size_t PushBack(size_t count);
  size_t PopFront(size_t count);

  void PushBack(const T &v);

  T *Begin();
  T *End();

  size_t Size() const;
  size_t Capacity() const;
  void Resize(size_t);

  void Clear();

private:
  size_t m_buffer_size;
  T *m_buffer;
  T *m_read;
  T *m_write;
  size_t m_capacity;
};

template <class T> CircularBuffer<T>::CircularBuffer(size_t num_elements) {
  num_elements += 1; // for last-first spacing

  m_buffer_size = sizeof(T) * num_elements;
  m_buffer = (T *)Allocate(&m_buffer_size);
  assert(m_buffer_size % sizeof(T) == 0);

  m_capacity = m_buffer_size / sizeof(T);

  m_read = m_buffer;
  m_write = m_buffer;
}

template <class T> CircularBuffer<T>::~CircularBuffer() {
  Deallocate(m_buffer, m_buffer_size);
}

template <class T> size_t CircularBuffer<T>::PushBack(size_t count) {
  if(count > Capacity()) {
    count = Capacity();
  }

  size_t elements_left = m_capacity - Size() - 1;
  if(count > elements_left) {
    PopFront(count - elements_left);
  }

  m_write += count;
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
  return count;
}

template <class T> T *CircularBuffer<T>::Begin() { return m_read; }

template <class T> T *CircularBuffer<T>::End() { return m_write; }

template <class T> size_t CircularBuffer<T>::Size() const {
  assert(m_write >= m_read);
  return m_write - m_read;
}
template <class T> size_t CircularBuffer<T>::Capacity() const {
  return m_capacity - 1;
}

template <class T> void CircularBuffer<T>::Resize(size_t size) {
  PushBack(size);
}

template <class T> void CircularBuffer<T>::PushBack(const T &v) {
  T *e = End();
  assert(e < m_buffer + m_buffer_size);
  *e = v;
  PushBack((size_t)1);
}

template <class T> void CircularBuffer<T>::Clear() {
  m_read = m_buffer;
  m_write = m_buffer;
}