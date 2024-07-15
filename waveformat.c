/*****************************************************************
 # waveformat.c: RIFF/WAVE format
 *****************************************************************
 * libhx2: library for reading and writing ubi hxaudio files
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#include "waveformat.h"

void waveformat_default_header(struct waveformat_header *header) {
  header->riff_id = WAVE_RIFF_CHUNK_ID;
  header->riff_length = 0;
  header->wave_id = WAVE_WAVE_CHUNK_ID;
  header->format_id = WAVE_FORMAT_CHUNK_ID;
  header->chunk_size = 16;
  header->format = 1; /* pcm */
  header->num_channels = 1;
  header->sample_rate = 22050;
  header->bytes_per_second = 0;
  header->block_alignment = 16;
  header->bits_per_sample = 16;
  header->subchunk2_id = WAVE_DATA_CHUNK_ID;
  header->subchunk2_size = 0;
}

int waveformat_header_rw(hx_stream_t *s, struct waveformat_header *header) {
  hx_stream_rw32(s, &header->riff_id);
  hx_stream_rw32(s, &header->riff_length);
  hx_stream_rw32(s, &header->wave_id);
  hx_stream_rw32(s, &header->format_id);
  hx_stream_rw32(s, &header->chunk_size);
  hx_stream_rw16(s, &header->format);
  hx_stream_rw16(s, &header->num_channels);
  hx_stream_rw32(s, &header->sample_rate);
  hx_stream_rw32(s, &header->bytes_per_second);
  hx_stream_rw16(s, &header->block_alignment);
  hx_stream_rw16(s, &header->bits_per_sample);
  hx_stream_rw32(s, &header->subchunk2_id);
  hx_stream_rw32(s, &header->subchunk2_size);
  return (header->riff_id == WAVE_RIFF_CHUNK_ID)
  && (header->wave_id == WAVE_WAVE_CHUNK_ID)
  && (header->format_id == WAVE_FORMAT_CHUNK_ID);
}

int waveformat_rw(hx_stream_t *s, struct waveformat_header *header, void* data) {
  header->riff_length = header->subchunk2_size + sizeof(struct waveformat_header) - 8;
  if (!waveformat_header_rw(s, header)) return 0;
  hx_stream_rw(s, data, header->subchunk2_size);
  return header->subchunk2_size + sizeof(struct waveformat_header);
}
