/*****************************************************************
 # waveformat.h: RIFF/WAVE format
 *****************************************************************
 * libhx2: library for reading and writing ubi hxaudio files
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#ifndef waveformat_h
#define waveformat_h

#include "stream.h"

#define WAVE_RIFF_CHUNK_ID      0x46464952 /* "RIFF" */
#define WAVE_WAVE_CHUNK_ID      0x45564157 /* "WAVE" */
#define WAVE_FORMAT_CHUNK_ID    0x20746D66 /* "fmt " */
#define WAVE_DATA_CHUNK_ID      0x61746164 /* "data" */
#define WAVE_EXT_DATA_CHUNK_ID  0x78746164 /* "datx" */
#define WAVE_CUE_CHUNK_ID       0x63756520 /* "cue " */

struct waveformat_chunk {
  unsigned int id;
  unsigned int size;
};

struct waveformat_data_chunk {
  unsigned int id;
  unsigned int size;
};

struct waveformat_cue_chunk {
  unsigned int id;
  unsigned int size;
  unsigned int num_cue_points;
};

struct waveformat_cue_point {
  unsigned int id;
  unsigned int position;
  unsigned int chunk_id;
  unsigned int chunk_start;
  unsigned int block_start;
  unsigned int sample_offset;
};

struct waveformat_header {
  unsigned int riff_id;
  unsigned int riff_length;
  unsigned int wave_id;
  unsigned int format_id;
  unsigned int chunk_size;
  unsigned short format;
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
void waveformat_default_header(struct waveformat_header *header);

/** waveformat_header_rw:
 * Read or write a wave format header. */
int waveformat_header_rw(hx_stream_t *s, struct waveformat_header *header);

/** waveformat_rw:
 * Read or write wave format header + data (size of `subchunk2_size`) */
int waveformat_rw(hx_stream_t *s, struct waveformat_header *header, void* data);

#endif /* waveformat_h */
