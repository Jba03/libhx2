/*****************************************************************
 # stream.h: Byte stream
 *****************************************************************
 * libhx2: library for reading and writing .hx audio files
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#ifndef stream_h
#define stream_h

#include <stdlib.h>
#include <string.h>

#define HX_BIG_ENDIAN 1
#define HX_LITTLE_ENDIAN 0
#define HX_NATIVE_ENDIAN (!*(unsigned char*)&(unsigned short){1})

#define HX_BYTESWAP16(data) ((unsigned short)((unsigned short)(data) << 8 | (unsigned short)(data) >> 8))
#define HX_BYTESWAP32(data) ((unsigned int)(HX_BYTESWAP16((unsigned int)(data)) << 16 | HX_BYTESWAP16((unsigned int)(data) >> 16)))

#define STREAM_MODE_READ 0
#define STREAM_MODE_WRITE 1

typedef struct stream {
  unsigned char mode;
  unsigned int size;
  unsigned int pos;
  unsigned char endianness;
  char* buf;
} stream_t;

stream_t stream_create(void* data, unsigned int size, unsigned char mode, unsigned char endianness);
stream_t stream_alloc(unsigned int size, unsigned char mode, unsigned char endianness);
void stream_seek(stream_t *s, unsigned int pos);
void stream_advance(stream_t *s, signed int offset);
void stream_rw(stream_t *s, void* data, unsigned int size);
void stream_rw8(stream_t *s, void *data);
void stream_rw16(stream_t *s, void *data);
void stream_rw32(stream_t *s, void *data);
void stream_rwfloat(stream_t *s, void *data);
void stream_rwcuuid(stream_t *s, void *cuuid);
void stream_dealloc(stream_t *s);

#endif /* stream_h */
