#ifndef _TEENSY41_ASYNC_TCP_CBUF_IMPL_H_
#define _TEENSY41_ASYNC_TCP_CBUF_IMPL_H_

#include "cbuf.hpp"

inline cbuf::cbuf(size_t size) : next(NULL), _size(size), _buf(new char[size]), _bufend(_buf + size), _begin(_buf), _end(_begin) {}

inline cbuf::~cbuf() {
  delete[] _buf;
}

inline size_t cbuf::resizeAdd(size_t addSize) {
  return resize(_size + addSize);
}

inline size_t cbuf::resize(size_t newSize) {
  size_t bytes_available = available();
  if ((newSize <= bytes_available) || (newSize == _size)) return _size;
  char *newbuf = new char[newSize];
  char *oldbuf = _buf;
  if (!newbuf) return _size;
  if (_buf) {
    read(newbuf, bytes_available);
    memset((newbuf + bytes_available), 0x00, (newSize - bytes_available));
  }
  _begin = newbuf;
  _end = newbuf + bytes_available;
  _bufend = newbuf + newSize;
  _size = newSize;
  _buf = newbuf;
  delete[] oldbuf;
  return _size;
}

inline size_t cbuf::available() const {
  if (_end >= _begin) return _end - _begin;
  return _size - (_begin - _end);
}

inline size_t cbuf::size() {
  return _size;
}

inline size_t cbuf::room() const {
  if (_end >= _begin) return _size - (_end - _begin) - 1;
  return _begin - _end - 1;
}

inline int cbuf::peek() {
  if (empty()) return -1;
  return static_cast<int>(*_begin);
}

inline size_t cbuf::peek(char *dst, size_t size) {
  size_t bytes_available = available();
  size_t size_to_read = (size < bytes_available) ? size : bytes_available;
  size_t size_read = size_to_read;
  char * begin = _begin;
  if (_end < _begin && size_to_read > (size_t) (_bufend - _begin)) {
    size_t top_size = _bufend - _begin;
    memcpy(dst, _begin, top_size);
    begin = _buf;
    size_to_read -= top_size;
    dst += top_size;
  }
  memcpy(dst, begin, size_to_read);
  return size_read;
}

inline int cbuf::read() {
  if (empty()) return -1;
  char result = *_begin;
  _begin = wrap_if_bufend(_begin + 1);
  return static_cast<int>(result);
}

inline size_t cbuf::read(char* dst, size_t size) {
  size_t bytes_available = available();
  size_t size_to_read = (size < bytes_available) ? size : bytes_available;
  size_t size_read = size_to_read;
  if (_end < _begin && size_to_read > (size_t) (_bufend - _begin)) {
    size_t top_size = _bufend - _begin;
    memcpy(dst, _begin, top_size);
    _begin = _buf;
    size_to_read -= top_size;
    dst += top_size;
  }
  memcpy(dst, _begin, size_to_read);
  _begin = wrap_if_bufend(_begin + size_to_read);
  return size_read;
}

inline size_t cbuf::write(char c) {
  if (full()) return 0;
  *_end = c;
  _end = wrap_if_bufend(_end + 1);
  return 1;
}

inline size_t cbuf::write(const char* src, size_t size) {
  size_t bytes_available = room();
  size_t size_to_write = (size < bytes_available) ? size : bytes_available;
  size_t size_written = size_to_write;
  if (_end >= _begin && size_to_write > (size_t) (_bufend - _end)) {
    size_t top_size = _bufend - _end;
    memcpy(_end, src, top_size);
    _end = _buf;
    size_to_write -= top_size;
    src += top_size;
  }
  memcpy(_end, src, size_to_write);
  _end = wrap_if_bufend(_end + size_to_write);
  return size_written;
}

inline void cbuf::flush() {
  _begin = _buf;
  _end = _buf;
}

inline size_t cbuf::remove(size_t size) {
  size_t bytes_available = available();
  if (size >= bytes_available) {
    flush();
    return 0;
  }
  size_t size_to_remove = (size < bytes_available) ? size : bytes_available;
  if (_end < _begin && size_to_remove > (size_t) (_bufend - _begin)) {
    size_t top_size = _bufend - _begin;
    _begin = _buf;
    size_to_remove -= top_size;
  }
  _begin = wrap_if_bufend(_begin + size_to_remove);
  return available();
}

#endif // _TEENSY41_ASYNC_TCP_CBUF_IMPL_H_