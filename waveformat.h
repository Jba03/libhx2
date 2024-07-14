/*****************************************************************
 # wave.h: RIFF/WAVE format
 *****************************************************************
 * libhx2: library for reading and writing ubi hxaudio files
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#ifndef wave_h
#define wave_h

#include "stream.h"

#define WAVE_RIFF_CHUNK_ID 0x46464952
#define WAVE_WAVE_CHUNK_ID 0x45564157
#define WAVE_FORMAT_CHUNK_ID 0x20746D66
#define WAVE_DATA_CHUNK_ID 0x61746164

struct wave_chunk_header {
  unsigned int id;
  unsigned int size;
};

struct wave_riff_chunk {
  //struct wave_chunk header;
  unsigned int riff_id;
};

struct wave_cue_chunk {
 // struct wave_chunk header;
  unsigned int code;
  unsigned int size;
  unsigned int format;
};

struct wave_header {
  unsigned int riff_id;
  unsigned int riff_length;
  unsigned int wave_id;
  unsigned int format_id;
  unsigned int chunk_size;
  unsigned short codec;
  unsigned short num_channels;
  unsigned int sample_rate;
  unsigned int bytes_per_second;
  unsigned short block_alignment;
  unsigned short bits_per_sample;
  unsigned int subchunk2_id;
  unsigned int subchunk2_size;
};

/** waveformat_default_header:
 * Set wave format header defaults. */
void waveformat_default_header(struct wave_header *header);

/** waveformat_header_rw:
 * Read or write a wave format header. */
int waveformat_header_rw(hx_stream_t *s, struct wave_header *header);

/** waveformat_rw:
 * Read or write wave format header + data (size of `subchunk2_size`) */
int waveformat_rw(hx_stream_t *s, struct wave_header *header, void* data);

#endif /* waveformat_h */
