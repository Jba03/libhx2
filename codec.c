/*****************************************************************
 # codec.c: Encoders/Decoders
 *****************************************************************
 * libhx2: library for reading and writing ubi hxaudio files
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#include "hx2.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#pragma mark - DSP ADPCM

#define DSP_SAMPLES_PER_FRAME 14

struct dsp_adpcm {
  unsigned int num_samples, num_nibbles, sample_rate;
  unsigned short loop_flag, format;
  unsigned int loop_start, loop_end, ca;
  signed short c[16], gain, ps, hst1, hst2;
  signed short loop_ps, loop_hst1, loop_hst2;
  /* internal */
  signed int history1, history2;
  unsigned int remaining;
};

static void dsp_adpcm_header_rw(hx_stream_t *s, struct dsp_adpcm *adpcm) {
  hx_stream_rw32(s, &adpcm->num_samples);
  hx_stream_rw32(s, &adpcm->num_nibbles);
  hx_stream_rw32(s, &adpcm->sample_rate);
  hx_stream_rw16(s, &adpcm->loop_flag);
  hx_stream_rw16(s, &adpcm->format);
  hx_stream_rw32(s, &adpcm->loop_start);
  hx_stream_rw32(s, &adpcm->loop_end);
  hx_stream_rw32(s, &adpcm->ca);
  for (int i = 0; i < 16; i++) hx_stream_rw16(s, &adpcm->c[i]);
  hx_stream_rw16(s, &adpcm->gain);
  hx_stream_rw16(s, &adpcm->ps);
  hx_stream_rw16(s, &adpcm->hst1);
  hx_stream_rw16(s, &adpcm->hst2);
  hx_stream_rw16(s, &adpcm->loop_ps);
  hx_stream_rw16(s, &adpcm->loop_hst1);
  hx_stream_rw16(s, &adpcm->loop_hst2);
  hx_stream_advance(s, 11 * 2); /* padding */
}

unsigned int dsp_pcm_size(unsigned int sample_count) {
  unsigned int frames = sample_count / DSP_SAMPLES_PER_FRAME;
  if (sample_count % DSP_SAMPLES_PER_FRAME) frames++;
  return frames * DSP_SAMPLES_PER_FRAME * sizeof(short);
}

int dsp_decode(hx_t *hx, hx_audio_stream_t *in, hx_audio_stream_t *out) {
  hx_stream_t stream = hx_stream_create(in->data, in->size, HX_STREAM_MODE_READ, in->endianness);
  
  unsigned int total_samples = 0;
  struct dsp_adpcm channels[in->num_channels];
  for (int c = 0; c < in->num_channels; c++) {
    struct dsp_adpcm* channel = &channels[c];
    dsp_adpcm_header_rw(&stream, channel);
    total_samples += channels[c].num_samples;
    channels[c].remaining = channels[c].num_samples;
  }
  
  out->sample_rate = in->sample_rate;
  out->codec = HX_CODEC_PCM;
  out->num_channels = in->num_channels;
  out->num_samples = total_samples;
  out->size = dsp_pcm_size(total_samples);
  out->data = malloc(out->size);
  
  short* dst = out->data;
  char* src = stream.buf + stream.pos;
  int num_frames = ((total_samples + DSP_SAMPLES_PER_FRAME - 1) / DSP_SAMPLES_PER_FRAME);
  
  for (int i = 0; i < num_frames; i++) {
    for (int c = 0; c < out->num_channels; c++) {
      struct dsp_adpcm *adpcm = channels + c;
      const signed char ps = *src++;
      const signed int predictor = (ps >> 4) & 0xF;
      const signed int scale = 1 << (ps & 0xF);
      const signed short c1 = adpcm->c[predictor * 2 + 0];
      const signed short c2 = adpcm->c[predictor * 2 + 1];
      
      signed int hst1 = adpcm->history1;
      signed int hst2 = adpcm->history2;
      signed int count = (adpcm->remaining > DSP_SAMPLES_PER_FRAME) ? DSP_SAMPLES_PER_FRAME : adpcm->remaining;
      
      for (int s = 0; s < count; s++) {
        int sample = (s % 2) == 0 ? ((*src >> 4) & 0xF) : (*src++ & 0xF);
        sample = sample >= 8 ? sample - 16 : sample;
        sample = (((scale * sample) << 11) + 1024 + (c1*hst1 + c2*hst2)) >> 11;
        if (sample < INT16_MIN) sample = INT16_MIN;
        if (sample > INT16_MAX) sample = INT16_MAX;
        hst2 = hst1;
        dst[s * out->num_channels + c] = hst1 = sample;
      }
          
      adpcm->history1 = hst1;
      adpcm->history2 = hst2;
      adpcm->remaining -= count;
    }
    
    dst += DSP_SAMPLES_PER_FRAME * out->num_channels;
  }
  
  return 1;
}

int ngc_dsp_encode(hx_t *hx, hx_audio_stream_t *in, hx_audio_stream_t *out) {
  return 0;
}
