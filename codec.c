/*****************************************************************
 # codec.c: Encoders/Decoders
 *****************************************************************
 * libhx2: library for reading and writing .hx audio files
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#include <limits.h>

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

int hx_error(const HX_Context *, const char* format, ...);

static void audio_stream_info_copy(struct HX_AudioStreamInfo *dst, const struct HX_AudioStreamInfo *src) {
  memcpy(dst, src, sizeof(struct HX_AudioStreamInfo));
}

#pragma mark - DSP ADPCM

#define DSP_HEADER_SIZE 96
#define DSP_BYTES_PER_FRAME 8
#define DSP_NIBBLES_PER_FRAME 16
#define DSP_SAMPLES_PER_FRAME 14

struct dsp_adpcm {
  unsigned int num_samples, num_nibbles, sample_rate;
  unsigned short loop_flag, format;
  unsigned int loop_start, loop_end, ca;
  signed short c[16], gain, ps, hst1, hst2;
  signed short loop_ps, loop_hst1, loop_hst2;
  signed int history1, history2, remaining; /* internal */
};

static void dsp_adpcm_header_rw(stream_t *s, struct dsp_adpcm *adpcm) {
  stream_rw32(s, &adpcm->num_samples);
  stream_rw32(s, &adpcm->num_nibbles);
  stream_rw32(s, &adpcm->sample_rate);
  stream_rw16(s, &adpcm->loop_flag);
  stream_rw16(s, &adpcm->format);
  stream_rw32(s, &adpcm->loop_start);
  stream_rw32(s, &adpcm->loop_end);
  stream_rw32(s, &adpcm->ca);
  for (int i = 0; i < 16; i++) stream_rw16(s, &adpcm->c[i]);
  stream_rw16(s, &adpcm->gain);
  stream_rw16(s, &adpcm->ps);
  stream_rw16(s, &adpcm->hst1);
  stream_rw16(s, &adpcm->hst2);
  stream_rw16(s, &adpcm->loop_ps);
  stream_rw16(s, &adpcm->loop_hst1);
  stream_rw16(s, &adpcm->loop_hst2);
  stream_advance(s, 11 * 2); /* padding */
}

static int dsp_nibble_count(int samples) {
  int frames = samples / DSP_SAMPLES_PER_FRAME;
  int extra_samples = samples % DSP_SAMPLES_PER_FRAME;
  int extra_nibbles = extra_samples == 0 ? 0 : extra_samples + 2;
  return DSP_NIBBLES_PER_FRAME * frames + extra_nibbles;
}

static int dsp_nibble_address(int sample) {
  int frames = sample / DSP_SAMPLES_PER_FRAME;
  int extra_samples = sample % DSP_SAMPLES_PER_FRAME;
  return DSP_NIBBLES_PER_FRAME * frames + extra_samples + 2;
}

static int dsp_byte_count(int samples) {
  int frames = samples / DSP_SAMPLES_PER_FRAME, extra_bytes = 0;
  int extra_samples = samples % DSP_SAMPLES_PER_FRAME;
  if (extra_samples != 0) extra_bytes = (extra_samples / 2) + (extra_samples % 2) + 1;
  return DSP_BYTES_PER_FRAME * frames + extra_bytes;
}

/** Size of decoded dsp stream */
static HX_Size dsp_pcm_size(const HX_Size sample_count) {
  unsigned int frames = sample_count / DSP_SAMPLES_PER_FRAME;
  if (sample_count % DSP_SAMPLES_PER_FRAME) frames++;
  return frames * DSP_SAMPLES_PER_FRAME * sizeof(short);
}

static int dsp_decode(const HX_AudioStream *in, HX_AudioStream *out) {
  stream_t stream = stream_create(in->data, in->size, STREAM_MODE_READ, in->info.endianness);
  
  unsigned int total_samples = 0;
  struct dsp_adpcm channels[in->info.num_channels];
  for (int c = 0; c < in->info.num_channels; c++) {
    struct dsp_adpcm* channel = &channels[c];
    dsp_adpcm_header_rw(&stream, channel);
    total_samples += channels[c].num_samples;
    channels[c].remaining = channels[c].num_samples;
  }
  
  audio_stream_info_copy(&out->info, &in->info);
  out->info.fmt = HX_AUDIO_FORMAT_PCM;
  out->info.num_samples = total_samples;
  out->size = dsp_pcm_size(total_samples);
  out->data = malloc(out->size);
  
  short* dst = out->data;
  char* src = stream.buf + stream.pos;
  int num_frames = ((total_samples + DSP_SAMPLES_PER_FRAME - 1) / DSP_SAMPLES_PER_FRAME);
  
  for (int i = 0; i < num_frames; i++) {
    for (int c = 0; c < out->info.num_channels; c++) {
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
        if (sample < SHRT_MIN) sample = SHRT_MIN;
        if (sample > SHRT_MAX) sample = SHRT_MAX;
        hst2 = hst1;
        dst[s * out->info.num_channels + c] = hst1 = sample;
      }
          
      adpcm->history1 = hst1;
      adpcm->history2 = hst2;
      adpcm->remaining -= count;
    }
    
    dst += DSP_SAMPLES_PER_FRAME * out->info.num_channels;
  }
  
  return 1;
}

static void dsp_frame_encode(signed short pcm[16], unsigned int num_samples, signed char adpcm[DSP_BYTES_PER_FRAME]) {
  int inSamples[16] = { pcm[0], pcm[1] };
  int outSamples[14], scale, v1, v2, v3, index, distance;
  double acc = 0.0f;
  
  for (int s = 0; s < num_samples; s++) {
    inSamples[s + 2] = v1 = ((pcm[s] * 0.0f) + (pcm[s + 1] * 0.0f)) / 2048;
    v2 = pcm[s + 2] - v1;
    v3 = (v2 >= SHRT_MAX) ? SHRT_MAX : (v2 <= SHRT_MIN) ? SHRT_MIN : v2;
    if (abs(v3) > abs(distance)) distance = v3;
  }
  
  for (scale = 0; (scale <= 12) && ((distance > 7) || (distance < -8)); scale++) distance /= 2;
  scale = (scale <= 1) ? -1 : scale - 2;

  do {
    scale++;
    acc = index = 0;
    for (int s = 0; s < num_samples; s++) {
      v1 = ((inSamples[s] * 0.0f) + (inSamples[s + 1] * 0.0f));
      v2 = (pcm[s + 2] << 11) - v1;
      v3 = (v2 > 0) ? (int)((double)v2 / (1 << scale) / 2048 + 0.5f) : (int)((double)v2 / (1 << scale) / 2048 - 0.5f);
      
      if (v3<-8) {
        if (index < (v3=-8-v3)) index = v3; v3 = -8;
      } else if (v3 > 7) {
        if (index < (v3-=7)) index = v3; v3 = 7;
      }
      
      outSamples[s] = v3;
      v1 = (v1 + ((v3 * (1 << scale)) << 11) + 1024) >> 11;
      inSamples[s+2] = v2 = (v1 >= SHRT_MAX) ? SHRT_MAX : (v1 <= SHRT_MIN) ? SHRT_MIN : v1;
      v3 = pcm[s+2] - v2;
      acc += v3 * (double)v3;
    }
    for (int x = index + 8; x > 256; x >>= 1) if (++scale >= 12) scale = 11;
  } while (scale < 12 && index > 1);
  
  adpcm[0] = scale & 0xF;
  for (int s = num_samples; s < 14; s++) outSamples[s] = 0;
  for (int y = 0; y < 7; y++) adpcm[y+1] = (char)((outSamples[y*2]<<4) | (outSamples[y*2+1]&0xF));
}

static int dsp_encode(const HX_AudioStream *in, HX_AudioStream *out) {
  unsigned int num_samples = in->info.num_samples;
  unsigned int framecount = (num_samples / DSP_SAMPLES_PER_FRAME) + (num_samples % DSP_SAMPLES_PER_FRAME != 0);
  unsigned int output_stream_size = framecount * DSP_BYTES_PER_FRAME * in->info.num_channels + in->info.num_channels * DSP_HEADER_SIZE;
  output_stream_size *= 2;
  
  audio_stream_info_copy(&out->info, &in->info);
  out->info.fmt = HX_AUDIO_FORMAT_DSP;
  out->info.endianness = HX_BIG_ENDIAN;
  
  stream_t output_stream = stream_alloc(output_stream_size, STREAM_MODE_WRITE, out->info.endianness);
  out->data = (signed short*)output_stream.buf;
  out->size = output_stream_size;
  
  stream_seek(&output_stream, out->info.num_channels * DSP_HEADER_SIZE);

  signed short samples[16];
  signed short* src = in->data;
  struct dsp_adpcm header[out->info.num_channels];
  
  for (unsigned int n = 0; n < framecount; n++) {
    for (unsigned int channel = 0; channel < out->info.num_channels; channel++) {
      
      unsigned int samples_to_process = min(num_samples - n * DSP_SAMPLES_PER_FRAME, DSP_SAMPLES_PER_FRAME);
      memset(samples + 2, 0, DSP_SAMPLES_PER_FRAME * sizeof(short));
      
      for (int s = 0; s < samples_to_process; ++s)
        samples[s + 2] = src[n * DSP_SAMPLES_PER_FRAME * out->info.num_channels + (channel + s * out->info.num_channels)];
      
      signed char frame[8];
      dsp_frame_encode(samples, DSP_SAMPLES_PER_FRAME, frame);
      
      if (n == 0) {
        header[channel].num_samples = out->info.num_samples;
        header[channel].num_nibbles = dsp_nibble_count(num_samples);
        header[channel].sample_rate = out->info.sample_rate;
        header[channel].loop_start = dsp_nibble_address(0);
        header[channel].loop_end = dsp_nibble_address(num_samples - 1);
        header[channel].ca = dsp_nibble_address(0);
        header[channel].ps = frame[0];
        memset(header[channel].c, 0, 16 * sizeof(short));
      }
      stream_rw(&output_stream, frame, dsp_byte_count(samples_to_process));
    }
  }
  
  out->size = output_stream.pos;
  
  /* Write headers */
  stream_seek(&output_stream, 0);
  for (int i = 0; i < out->info.num_channels; i++)
    dsp_adpcm_header_rw(&output_stream, header + i);
  
  return 0;
}


#pragma mark - PSX ADPCM

#define PSX_BYTES_PER_FRAME 16
#define PSX_SAMPLE_BYTES_PER_FRAME 14
#define PSX_SAMPLES_PER_FRAME 28

static const float psx_adpcm_coefficients[16][2] = {
  { 0.0       ,  0.0       },
  { 0.9375    ,  0.0       },
  { 1.796875  , -0.8125    },
  { 1.53125   , -0.859375  },
  { 1.90625   , -0.9375    },
  { 0.46875   , -0.0       },
  { 0.8984375 , -0.40625   },
  { 0.765625  , -0.4296875 },
  { 0.953125  , -0.46875   },
  { 0.234375  , -0.0       },
  { 0.44921875, -0.203125  },
  { 0.3828125 , -0.21484375},
  { 0.4765625 , -0.234375  },
  { 0.5       , -0.9375    },
  { 0.234375  , -0.9375    },
  { 0.109375  , -0.9375    },
};

static const HX_Size psx_sample_count(const HX_Size sz, const int ch) {
  return sz / ch / PSX_BYTES_PER_FRAME * PSX_SAMPLES_PER_FRAME;
}

/** Size of decoded psx stream */
static const HX_Size psx_pcm_size(const HX_Size sample_count) {
  unsigned int frames = sample_count / PSX_SAMPLES_PER_FRAME;
  if (sample_count % PSX_SAMPLES_PER_FRAME) frames++;
  return frames * PSX_SAMPLES_PER_FRAME * sizeof(short);
}

static int psx_decode(const HX_AudioStream *in, HX_AudioStream *out) {
  stream_t stream = stream_create(in->data, in->size, STREAM_MODE_READ, in->info.endianness);
  const HX_Size total_samples = psx_sample_count(in->size, in->info.num_channels);
  
  signed int history[in->info.num_channels][2];
  memset(history, 0, in->info.num_channels * 2);
  
  audio_stream_info_copy(&out->info, &in->info);
  out->info.fmt = HX_AUDIO_FORMAT_PCM;
  out->info.num_samples = total_samples;
  out->size = psx_pcm_size(total_samples);
  out->data = malloc(out->size);
  
  short* dst = out->data;
  char* src = stream.buf + stream.pos;
  int num_frames = ((total_samples + PSX_SAMPLES_PER_FRAME - 1) / PSX_SAMPLES_PER_FRAME);
  
  for (int i = 0; i < num_frames; dst += PSX_SAMPLES_PER_FRAME * out->info.num_channels, i++) {
    for (int c = 0; c < out->info.num_channels; src += PSX_SAMPLE_BYTES_PER_FRAME, c++) {
      const unsigned char predict = (*src >> 4) & 0xF;
      const unsigned char shift = (*src++ >> 0) & 0xF;
      const unsigned char __unused flags = (*src++);
      
      if (predict > 4) return -1;
      
      signed int hst1 = history[c][0];
      signed int hst2 = history[c][1];
      signed int expanded[PSX_SAMPLES_PER_FRAME];
      for (signed int y=0; y<PSX_SAMPLE_BYTES_PER_FRAME; y++) {
        expanded[y*2+0] = (src[y] & 0xF);
        expanded[y*2+1] = (src[y] & 0xF0) >> 4;
      }
      
      for (signed int s=0; s<PSX_SAMPLES_PER_FRAME; s++) {
        int sample = expanded[s] << 12;
        if ((sample & 0x8000) != 0) {
          sample = (int)(sample | 0xFFFF0000);
        }
        
        double output = (sample >> shift) + hst1 * psx_adpcm_coefficients[predict][0] + hst2 * psx_adpcm_coefficients[predict][1];
        hst2 = hst1;
        dst[s * out->info.num_channels + c] = hst1 = ((short)(min(SHRT_MAX, max(output, SHRT_MIN))));
      }
      
      history[c][0] = hst1;
      history[c][1] = hst2;
    }
  }
  
  return 1;
}
