/*****************************************************************
 # stream.c: Data stream
 *****************************************************************
 * libhx2: library for reading and writing ubi hxaudio files
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#include "stream.h"

#include <stdlib.h>
#include <string.h>

hx_stream_t hx_stream_create(void* data, unsigned int size, unsigned char mode, unsigned char endianness) {
  return (hx_stream_t){ .buf = data, .size = size, .pos = 0, .mode = mode, .endianness = endianness };
}

hx_stream_t hx_stream_alloc(unsigned int size, unsigned char mode, unsigned char endianness) {
  hx_stream_t s = hx_stream_create(malloc(size), size, mode, endianness);
  memset(s.buf, 0, size);
  return s;
}

static unsigned char doswap(hx_stream_t *s, unsigned char mode) {
  return (s->endianness != HX_NATIVE_ENDIAN) && (s->mode == mode);
}

void hx_stream_seek(hx_stream_t *s, unsigned int pos) {
  s->pos = pos;
}

void hx_stream_advance(hx_stream_t *s, signed int offset) {
  s->pos += offset;
}

void hx_stream_rw(hx_stream_t *s, void* data, unsigned int size) {
  if (s->mode == HX_STREAM_MODE_READ) memcpy(data, s->buf + s->pos, size);
  if (s->mode == HX_STREAM_MODE_WRITE) memcpy(s->buf + s->pos, data, size);
  hx_stream_advance(s, size);
}

void hx_stream_rw8(hx_stream_t *s, void *p) {
  unsigned char *data = p;
  hx_stream_rw(s, data, 1);
}

void hx_stream_rw16(hx_stream_t *s, void *p) {
  unsigned short *data = p;
  if (doswap(s, HX_STREAM_MODE_WRITE)) *data = HX_BYTESWAP16(*data);
  hx_stream_rw(s, data, 2);
  if (doswap(s, HX_STREAM_MODE_READ) || doswap(s, HX_STREAM_MODE_WRITE)) *data = HX_BYTESWAP16(*data);
}

void hx_stream_rw32(hx_stream_t *s, void *p) {
  unsigned int *data = p;
  if (doswap(s, HX_STREAM_MODE_WRITE)) *data = HX_BYTESWAP32(*data);
  hx_stream_rw(s, data, 4);
  if (doswap(s, HX_STREAM_MODE_READ) || doswap(s, HX_STREAM_MODE_WRITE)) *data = HX_BYTESWAP32(*data);
}

void hx_stream_rwfloat(hx_stream_t *s, void *p) {
  unsigned int *data = p;
  hx_stream_rw32(s, (unsigned int*)data);
}

void hx_stream_rwcuuid(hx_stream_t *s, void *p) {
  unsigned long long *cuuid = p;
  hx_stream_rw32(s, (unsigned int*)cuuid + 1);
  hx_stream_rw32(s, (unsigned int*)cuuid + 0);
}

void hx_stream_dealloc(hx_stream_t *s) {
  free(s->buf);
}
