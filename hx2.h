/*****************************************************************
 # hx2.h: Context declarations
 *****************************************************************
 * libhx2: library for reading and writing .hx audio files
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#ifndef hx2_h
#define hx2_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "waveformat.h"

typedef struct hx hx_t;
typedef unsigned long long hx_cuuid_t;

typedef char* (*hx_read_callback_t)(const char* filename, size_t pos, size_t *size, void* userdata);
typedef void (*hx_write_callback_t)(const char* filename, void* data, size_t pos, size_t *size, void* userdata);
typedef void (*hx_error_callback_t)(const char* error_str, void* userdata);

#define HX_STRING_MAX_LENGTH  256

#define HX_LANGUAGE_DE 0x20206564
#define HX_LANGUAGE_EN 0x20206E65
#define HX_LANGUAGE_ES 0x20207365
#define HX_LANGUAGE_FR 0x20207266
#define HX_LANGUAGE_IT 0x20207469

enum hx_version {
  HX_VERSION_HXD, /* M/Arena */
  HX_VERSION_HXC, /* R3 PC */
  HX_VERSION_HX2, /* R3 PS2 */
  HX_VERSION_HXG, /* R3 GCN */
  HX_VERSION_HXX, /* R3 XBOX (+HD) */
  HX_VERSION_HX3, /* R3 PS3 HD */
  HX_VERSION_INVALID,
};

enum hx_class {
  HX_CLASS_EVENT_RESOURCE_DATA,
  HX_CLASS_WAVE_RESOURCE_DATA,
  HX_CLASS_SWITCH_RESOURCE_DATA,
  HX_CLASS_RANDOM_RESOURCE_DATA,
  HX_CLASS_PROGRAM_RESOURCE_DATA,
  HX_CLASS_WAVE_FILE_ID_OBJECT,
  HX_CLASS_INVALID,
};

enum hx_codec {
  HX_CODEC_PCM  = 0x01, /* PCM */
  HX_CODEC_UBI  = 0x02, /* UBI ADPCM */
  HX_CODEC_PSX  = 0x03, /* PS ADPCM */
  HX_CODEC_DSP  = 0x04, /* GC 4-bit ADPCM */
  HX_CODEC_IMA  = 0x05, /* MS IMA ADPCM */
  HX_CODEC_MP3  = 0x55,
};

/** hx_codec_name:
 * Get the name of codec `c`. */
const char* hx_codec_name(enum hx_codec c);

typedef struct hx_audio_stream_info {
  unsigned char num_channels;
  unsigned char endianness;
  unsigned int sample_rate;
  unsigned int num_samples;
  enum hx_codec codec;
} hx_audio_stream_info_t;

typedef struct hx_audio_stream {
  hx_cuuid_t wavefile_cuuid;
  signed short* data;
  unsigned int size;
  hx_audio_stream_info_t info;
} hx_audio_stream_t;

/**
 * Set the default parameters of audio stream `s`.
 */
void hx_audio_stream_init(hx_audio_stream_t *s);

/**
 * Get the size of audio stream `s` in bytes.
 */
unsigned int hx_audio_stream_size(hx_audio_stream_t *s);

/**
 * Write audio stream to .wav file
 */
int hx_audio_stream_write_wav(hx_t *hx, hx_audio_stream_t *s, const char* filename);

/**
 * Convert audio data from stream `i_stream` into `o_stream`.
 * The parameters of the desired output format should be set in the output stream info.
 * Returns: 1 on success, 0 on encoding/decoding error, -1 on unsupported format.
 */
int hx_audio_convert(hx_audio_stream_t* i_stream, hx_audio_stream_t* o_stream);

#pragma mark - Class -

typedef struct hx_event_resource_data {
  char name[HX_STRING_MAX_LENGTH];
  unsigned int type;
  unsigned int flags;
  hx_cuuid_t link_cuuid;
  float f_param[4];
} hx_event_resource_data_t;

/**
 * WavResObj:
 *  Superclass to WavResData
 */
typedef struct hx_wav_resource_object {
  unsigned int id;
  unsigned int size;
  float c0;
  float c1;
  float c2;
  signed char flags;
  /* name (.hxc only?) */
  char name[HX_STRING_MAX_LENGTH];
} hx_wav_resource_object_t;

typedef struct hx_wav_resource_data_link {
  /** Language of the linked entry */
  unsigned int language;
  /** Link to WaveFileIdObj */
  hx_cuuid_t cuuid;
} hx_wav_resource_data_link_t;


/**
 * WavResData:
 *  Superclass to WavResData
 */
typedef struct hx_wav_resource_data {
  hx_wav_resource_object_t res_data;
  hx_cuuid_t default_cuuid;
  unsigned int num_links;
  hx_wav_resource_data_link_t* links;
} hx_wav_resource_data_t;

typedef struct hx_random_resource_data_link {
  /* cuuid of the linked resdata */
  hx_cuuid_t cuuid;
  /* probability of being played */
  float probability;
} hx_random_resource_data_link_t;

/**
 * RandomResData:
 *  Contains links to ResData objects
 *  with probabilities of being played.
 */
typedef struct hx_random_resource_data {
  unsigned int flags;
  /** Unknown offset */
  float offset;
  /** The probability of not playing at all */
  float throw_probability;
  /** Number of CResData links */
  unsigned int num_links;
  /** ResData links */
  hx_random_resource_data_link_t* links;
} hx_random_resource_data_t;

typedef struct hx_switch_resource_data_link {
  unsigned int case_index;
  hx_cuuid_t cuuid;
} hx_switch_resource_data_link_t;

typedef struct hx_switch_resource_data {
  unsigned int flag;
  unsigned int unknown;
  unsigned int unknown2;
  unsigned int start_index;
  unsigned int num_links;
  struct hx_switch_resource_data_link* links;
} hx_switch_resource_data_t;

typedef struct hx_program_resource_data {
  int num_links;
  hx_cuuid_t links[256];
  void* data;
} hx_program_resource_data_t;

typedef struct hx_id_object_pointer {
  unsigned int id;
  float unknown;
  /* */
  unsigned int flags;
  unsigned int unknown2;
} hx_id_object_pointer_t;

typedef struct hx_wave_file_id_object {
  struct hx_id_object_pointer id_obj;
  struct waveformat_header wave_header;
  
  /** Name of the stream */
  char name[HX_STRING_MAX_LENGTH];
  /** Filename of the external stream */
  char ext_stream_filename[HX_STRING_MAX_LENGTH];
  /** Size of the external stream */
  unsigned int ext_stream_size;
  /** Offset in the external stream */
  unsigned int ext_stream_offset;
  
  /** Audio stream */
  hx_audio_stream_t *audio_stream;
  
  void *extra_wave_data;
  signed int extra_wave_data_length;
  
} hx_wave_file_id_object_t;

#pragma mark - Context

typedef struct hx_entry_language_link {
  unsigned int code;
  unsigned int unknown;
  hx_cuuid_t cuuid;
} hx_entry_language_link_t;

typedef struct hx_entry {
  /** Unique identifier */
  hx_cuuid_t cuuid;
  /** The class of the entry object */
  enum hx_class i_class;
  /** Entry class data */
  void* data;
  
  /** Number of linked entries */
  unsigned int num_links;
  /** Linked entry CUUIDs */
  hx_cuuid_t* links;
  
  /** Number of languages */
  unsigned int num_languages;
  /** Language codes */
  hx_entry_language_link_t* language_links;
  
  /** File offset when writing */
  unsigned int file_offset;
  /** Entry size in bytes */
  unsigned int file_size;
  unsigned int tmp_file_size;
} hx_entry_t;

/**
 * Allocate an empty context.
 */
hx_t *hx_context_alloc();

/**
 * Set read, write and error callbacks for the specified context, with an optional userdata pointer.
 */
void hx_context_callback(hx_t *hx, hx_read_callback_t read, hx_write_callback_t write, hx_error_callback_t error, void* userdata);

/**
 * Load a hxaudio file (.hxd, .hxc, .hx2, .hxg, .hxx, .hx3) into context `hx`.
 */
int hx_context_open(hx_t *hx, const char* filename);

/**
 * Get all entries for the specified context
 */
void hx_context_get_entries(hx_t *hx, hx_entry_t** entries, int *count);

/**
 * Find entry by cuuid
 */
hx_entry_t *hx_context_entry_lookup(hx_t *hx, hx_cuuid_t cuuid);

/**
 * Get the name of a class `c`
 */
void hx_class_to_string(hx_t *hx, enum hx_class c, char *out, unsigned int *out_sz);

/**
 * Write a context to memory.
 */
void hx_context_write(hx_t *hx, const char* filename, enum hx_version version);

/**
 * Deallocate a context.
 */
void hx_context_free(hx_t **hx);

#ifdef __cplusplus
}
#endif

#endif /* hx2_h */
