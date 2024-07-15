/*****************************************************************
 # stream.h: Data stream
 *****************************************************************
 * libhx2: library for reading and writing ubi hxaudio files
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#ifndef stream_h
#define stream_h

#define HX_BIG_ENDIAN 1
#define HX_LITTLE_ENDIAN 0
#define HX_NATIVE_ENDIAN (!*(unsigned char*)&(unsigned short){1})

#define HX_BYTESWAP16(data) ((unsigned short)((unsigned short)(data) << 8 | (unsigned short)(data) >> 8))
#define HX_BYTESWAP32(data) ((unsigned int)(HX_BYTESWAP16((unsigned int)(data)) << 16 | HX_BYTESWAP16((unsigned int)(data) >> 16)))

#define HX_STREAM_MODE_READ 0
#define HX_STREAM_MODE_WRITE 1

typedef struct hx_stream {
  unsigned char mode;
  unsigned int size;
  unsigned int pos;
  unsigned char endianness;
  char* buf;
} hx_stream_t;

hx_stream_t hx_stream_create(void* data, unsigned int size, unsigned char mode, unsigned char endianness);
hx_stream_t hx_stream_alloc(unsigned int size, unsigned char mode, unsigned char endianness);

void hx_stream_seek(hx_stream_t *s, unsigned int pos);
void hx_stream_advance(hx_stream_t *s, signed int offset);
void hx_stream_rw(hx_stream_t *s, void* data, unsigned int size);
void hx_stream_rw8(hx_stream_t *s, void *data);
void hx_stream_rw16(hx_stream_t *s, void *data);
void hx_stream_rw32(hx_stream_t *s, void *data);
void hx_stream_rwfloat(hx_stream_t *s, void *data);
void hx_stream_rwcuuid(hx_stream_t *s, void *cuuid);

void hx_stream_dealloc(hx_stream_t *s);

#endif /* stream_h */
