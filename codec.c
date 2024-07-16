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

struct dsp_adpcm_channel {
  unsigned int num_samples, remaining;
  signed short coefficient[16];
  signed short yn1, yn2, loop_yn1, loop_yn2;
  unsigned short gain, ps, loop_ps;
  unsigned int hst1, hst2;
};

unsigned int dsp_pcm_size(unsigned int sample_count) {
  unsigned int frames = sample_count / DSP_SAMPLES_PER_FRAME;
  if (sample_count % DSP_SAMPLES_PER_FRAME) frames++;
  return frames * DSP_SAMPLES_PER_FRAME * sizeof(short);
}

int dsp_decode(hx_t *hx, hx_audio_stream_t *in, hx_audio_stream_t *out) {
  hx_stream_t stream = hx_stream_create(in->data, in->size, HX_STREAM_MODE_READ, in->endianness);
  
  unsigned int total_samples = 0;
  struct dsp_adpcm_channel channels[in->num_channels];
  for (int c = 0; c < in->num_channels; c++) {
    struct dsp_adpcm_channel* channel = &channels[c];
    hx_stream_rw32(&stream, &channel->num_samples);
    hx_stream_advance(&stream, 24);
    for (int i=0;i<16;i++) hx_stream_rw16(&stream, &channel->coefficient[i]);
    hx_stream_rw16(&stream, &channel->gain);
    hx_stream_rw16(&stream, &channel->ps);
    hx_stream_rw16(&stream, &channel->yn1);
    hx_stream_rw16(&stream, &channel->yn2);
    hx_stream_rw16(&stream, &channel->loop_ps);
    hx_stream_rw16(&stream, &channel->loop_yn1);
    hx_stream_rw16(&stream, &channel->loop_yn2);
    hx_stream_advance(&stream, 11 * 2);
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
      struct dsp_adpcm_channel *adpcm = channels + c;
      const signed char ps = *src++;
      const signed int predictor = (ps >> 4) & 0xF;
      const signed int scale = 1 << (ps & 0xF);
      const signed short c1 = adpcm->coefficient[predictor * 2 + 0];
      const signed short c2 = adpcm->coefficient[predictor * 2 + 1];
      
      signed int hst1 = adpcm->hst1;
      signed int hst2 = adpcm->hst2;
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
          
      adpcm->hst1 = hst1;
      adpcm->hst2 = hst2;
      adpcm->remaining -= count;
    }
    
    dst += DSP_SAMPLES_PER_FRAME * out->num_channels;
  }
  
  return 1;
}

int ngc_dsp_encode(hx_t *hx, hx_audio_stream_t *in, hx_audio_stream_t *out) {
  return 0;
}
