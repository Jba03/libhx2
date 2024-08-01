/*****************************************************************
 # stream.c: Byte stream
 *****************************************************************
 * libhx2: library for reading and writing .hx audio files
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#include "stream.h"

stream_t stream_create(void* data, unsigned int size, unsigned char mode, unsigned char endianness) {
  return (stream_t){ .buf = data, .size = size, .pos = 0, .mode = mode, .endianness = endianness };
}

stream_t stream_alloc(unsigned int size, unsigned char mode, unsigned char endianness) {
  stream_t s = stream_create(malloc(size), size, mode, endianness);
  memset(s.buf, 0, size);
  return s;
}

static unsigned char doswap(stream_t *s, unsigned char mode) {
  return (s->endianness != HX_NATIVE_ENDIAN) && (s->mode == mode);
}

void stream_seek(stream_t *s, unsigned int pos) {
  s->pos = pos;
}

void stream_advance(stream_t *s, signed int offset) {
  s->pos += offset;
}

void stream_rw(stream_t *s, void* data, unsigned int size) {
  if (s->mode == STREAM_MODE_READ) memcpy(data, s->buf + s->pos, size);
  if (s->mode == STREAM_MODE_WRITE) memcpy(s->buf + s->pos, data, size);
  stream_advance(s, size);
}

void stream_rw8(stream_t *s, void *p) {
  unsigned char *data = p;
  stream_rw(s, data, 1);
}

void stream_rw16(stream_t *s, void *p) {
  unsigned short *data = p;
  if (doswap(s, STREAM_MODE_WRITE)) *data = HX_BYTESWAP16(*data);
  stream_rw(s, data, 2);
  if (doswap(s, STREAM_MODE_READ) || doswap(s, STREAM_MODE_WRITE)) *data = HX_BYTESWAP16(*data);
}

void stream_rw32(stream_t *s, void *p) {
  unsigned int *data = p;
  if (doswap(s, STREAM_MODE_WRITE)) *data = HX_BYTESWAP32(*data);
  stream_rw(s, data, 4);
  if (doswap(s, STREAM_MODE_READ) || doswap(s, STREAM_MODE_WRITE)) *data = HX_BYTESWAP32(*data);
}

void stream_rwfloat(stream_t *s, void *p) {
  unsigned int *data = p;
  stream_rw32(s, (unsigned int*)data);
}

void stream_rwcuuid(stream_t *s, void *p) {
  unsigned long long *cuuid = p;
  stream_rw32(s, (unsigned int*)cuuid + 1);
  stream_rw32(s, (unsigned int*)cuuid + 0);
}

void stream_dealloc(stream_t *s) {
  free(s->buf);
}
