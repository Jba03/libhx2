/*****************************************************************
 # libhx2: library for reading and writing ubi hxaudio files
 *****************************************************************
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#ifndef hx_h
#define hx_h

#include <stddef.h>
#include <stdint.h>

typedef struct hx hx_t;
typedef struct hx_entry hx_entry_t;
typedef struct hx_audio_stream hx_audio_stream_t;

typedef uint8_t* (*hx_read_callback_t)(const char* filename, size_t pos, size_t *size);
typedef void (*hx_write_callback_t)(const char* filename, void* data, size_t pos, size_t *size);

enum hx_version {
  HX_VERSION_HXD, /* Rayman M/Arena */
  HX_VERSION_HXC, /* Rayman 3 PC */
  HX_VERSION_HX2, /* Rayman 3 PS2 */
  HX_VERSION_HXG, /* Rayman 3 GCN */
  HX_VERSION_HXX, /* Rayman 3 XBOX (+HD) */
  HX_VERSION_HX3, /* Rayman 3 PS3 HD */
  HX_VERSION_INVALID,
};

enum hx_class {
  HX_CLASS_EVENT_RESOURCE_DATA,
  HX_CLASS_WAVE_RESOURCE_DATA,
  HX_CLASS_RANDOM_RESOURCE_DATA,
  HX_CLASS_PROGRAM_RESOURCE_DATA,
  HX_CLASS_WAVE_FILE_ID_OBJECT,
  HX_CLASS_INVALID,
};

enum hx_codec {
  HX_CODEC_PCM  = 0x01, /* PCM */
  HX_CODEC_UBI  = 0x02, /* UBI ADPCM */
  HX_CODEC_PSX  = 0x03,
  HX_CODEC_DSP  = 0x04, /* Nintendo 4-bit ADPCM */
  HX_CODEC_XIMA = 0x05, /* Microsoft IMA ADPCM */
  HX_CODEC_MP3  = 0x55,
};

#pragma mark - Audio stream

struct hx_audio_stream {
  enum hx_codec codec;
  int16_t *data;
  uint32_t size;
  uint8_t num_channels;
  uint8_t endianness;
  uint32_t sample_rate;
  uint32_t num_samples;
};

/** hx_decode_ngc_dsp:
 * Decode NGC-DSP-ADPCM data into PCM samples.
 **/
int hx_decode_ngc_dsp(hx_t *hx, hx_audio_stream_t *in_dsp, hx_audio_stream_t *out_pcm);

/** hx_encode_ngc_dsp:
 * Encode PCM samples into NGC-DSP-ADPCM data.
 **/
int hx_encode_ngc_dsp(hx_t *hx, hx_audio_stream_t *in_pcm, hx_audio_stream_t *out_dsp);

int hx_audio_stream_write_wav(hx_t *hx, hx_audio_stream_t *s, const char* filename);

struct hx_waveformat_header {
  uint32_t riff_code;
  uint32_t riff_length;
  uint32_t wave_code;
  uint32_t fmt_code;
  uint32_t chunk_size;
  uint16_t format;
  uint16_t channels;
  uint32_t sample_rate;
  uint32_t bytes_per_second;
  uint16_t alignment;
  uint16_t bits_per_sample;
  uint32_t data_code;
  uint32_t data_length;
};

#pragma mark - Class -

struct hx_event_resource_data {
  char* name;
  uint32_t type;
  uint32_t flags;
  uint64_t link_cuuid;
  float f_param[4];
};

/**
 * WavResObj:
 *  Superclass to WavResData
 */
struct hx_wav_resource_object {
  uint32_t id;
  uint32_t size;
  float c0;
  float c1;
  float c2;
  uint8_t flags;
  /* name (.hxc only?) */
  char* name;
};

struct hx_wav_resource_data_link {
  char language_code[4];
  /** CUUID of linked WaveFileIdObj */
  uint64_t cuuid;
};


/**
 * WavResData:
 *  Superclass to WavResData
 */
struct hx_wav_resource_data {
  struct hx_wav_resource_object res_data;
  uint64_t default_cuuid;
  uint32_t num_links;
  struct hx_wav_resource_data_link* links;
};

struct hx_random_resource_data_link {
  /* cuuid of the linked resdata */
  uint64_t cuuid;
  /* probability of being played */
  float probability;
};

/**
 * RandomResData:
 *  Contains links to ResData objects
 *  with probabilities of being played.
 */
struct hx_random_resource_data {
  uint32_t flags;
  /** Unknown offset */
  float offset;
  /** The probability of not playing at all */
  float throw_probability;
  /** Number of CResData links */
  uint32_t num_links;
  /** ResData links */
  struct hx_random_resource_data_link *links;
};

struct hx_id_object_pointer {
  uint32_t id;
  float unknown;
  /* */
  uint32_t flags;
};

struct hx_wave_file_id_object {
  struct hx_id_object_pointer id_obj;
  struct hx_waveformat_header wave_header;
  
  uint32_t num_samples;
  
  /* filename of the external stream */
  char* ext_stream_filename;
  /* size of the external stream */
  uint32_t ext_stream_size;
  /* offset in the external stream */
  uint32_t ext_stream_offset;
  
  /* the audio stream */
  hx_audio_stream_t *audio_stream;
};

#pragma mark - Context

/* ctx options */
#define HX_OPT_MEMORY_LESS  (1 << 0) /* don't load resource files into memory */

struct hx_entry {
  /** Unique identifier */
  uint64_t cuuid;
  /** The class of the entry object */
  enum hx_class class;
  /** Entry class data */
  void* data;
};

/** hx_context_alloc:
 * Allocate an empty context.
 **/
hx_t *hx_context_alloc(int options);

/** hx_context_callback:
 * Set the read and write callbacks for the specified context.
 **/
void hx_context_callback(hx_t *hx, hx_read_callback_t read, hx_write_callback_t write);

/** hx_context_open:
 * Load a hxaudio file (.hxd, .hxc, .hx2, .hxg, .hxx, .hx3) into context `hx`.
 **/
int hx_context_open(hx_t *hx, const char* filename);

/** hx_context_open2:
 * Load a hxaudio file from memory into context `hx`.
 **/
int hx_context_open2(hx_t *hx, uint8_t* buf, size_t sz);

/** hx_context_get_entries:
 * Get the entries for the specified context
 */
void hx_context_get_entries(hx_t *hx, hx_entry_t** entries, int *count);

/** hx_context_entry_lookup:
 * Find an entry by cuuid
 **/
hx_entry_t *hx_context_entry_lookup(hx_t *hx, uint64_t cuuid);

/** hx_context_write:
 * Write a context to memory.
 **/
void hx_context_write(hx_t *hx, int write_opt);

/** hx_context_dealloc:
 * Deallocate a context.
 **/
void hx_context_dealloc(hx_t **hx);

/** hx_error_string:
 * Get the current error message.
 */
const char* hx_error_string(hx_t *hx);

#endif /* hx_h */
