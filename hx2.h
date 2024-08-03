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

typedef struct hx hx_t;
typedef unsigned long long hx_cuuid_t;
typedef unsigned int hx_size_t;

typedef char* (*hx_read_callback_t)(const char* filename, size_t pos, size_t *size, void* userdata);
typedef void (*hx_write_callback_t)(const char* filename, void* data, size_t pos, size_t *size, void* userdata);
typedef void (*hx_error_callback_t)(const char* error_str, void* userdata);

#define HX_STRING_MAX_LENGTH  256
#define HX_INVALID_CUUID 0

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

#pragma mark - Audio

enum hx_format {
  HX_FORMAT_PCM  = 0x01, /* PCM */
  HX_FORMAT_UBI  = 0x02, /* UBI ADPCM */
  HX_FORMAT_PSX  = 0x03, /* PS ADPCM */
  HX_FORMAT_DSP  = 0x04, /* GC 4-bit ADPCM */
  HX_FORMAT_IMA  = 0x05, /* MS IMA ADPCM */
  HX_FORMAT_MP3  = 0x55,
};

typedef struct hx_audio_stream_info {
  unsigned char num_channels;
  unsigned char endianness;
  unsigned int sample_rate;
  unsigned int num_samples;
  enum hx_format fmt;
} hx_audio_stream_info_t;

typedef struct hx_audio_stream {
  hx_size_t size;
  signed short* data;
  hx_audio_stream_info_t info;
  hx_cuuid_t wavefile_cuuid;
} hx_audio_stream_t;

/**
 * Get the name of audio format `c`.
 */
const char* hx_format_name(enum hx_format c);

/**
 * Set the default parameters of audio stream `s`.
 */
void hx_audio_stream_init(hx_audio_stream_t *s);

/**
 * Get the size of audio stream `s` in bytes.
 */
unsigned int hx_audio_stream_size(const hx_audio_stream_t *s);

/**
 * Write audio stream to .wav file
 */
int hx_audio_stream_write_wav(const hx_t *hx, hx_audio_stream_t *s, const char* filename);

/**
 * Convert audio data from stream `i_stream` into `o_stream`.
 * The parameters of the desired output format should be set in the output stream info.
 * Return: 1 on success, 0 on encoding/decoding error, -1 on unsupported format.
 */
int hx_audio_convert(const hx_audio_stream_t* i_stream, hx_audio_stream_t* o_stream);

#pragma mark - Class -

#define HX_LINK(...) struct { hx_cuuid_t cuuid; __VA_ARGS__; } *

enum hx_class {
  HX_CLASS_EVENT_RESOURCE_DATA,
  HX_CLASS_WAVE_RESOURCE_DATA,
  HX_CLASS_SWITCH_RESOURCE_DATA,
  HX_CLASS_RANDOM_RESOURCE_DATA,
  HX_CLASS_PROGRAM_RESOURCE_DATA,
  HX_CLASS_WAVE_FILE_ID_OBJECT,
  HX_CLASS_INVALID,
};

/**
 * EventResData:
 * An event called by the game to start or stop audio playback.
 */
typedef struct hx_event_resource_data {
  unsigned int type;
  /** The name of the event. Usually starts with 'Play_' or 'Stop_'. */
  char name[HX_STRING_MAX_LENGTH];
  /** Flags */
  unsigned int flags;
  /** The linked entry */
  hx_cuuid_t link;
  /** Unknown parameters. */
  float c[4];
} hx_event_resource_data_t;

/**
 * WavResObj:
 * Superclass to WavResData
 */
typedef struct hx_wav_resource_object {
  unsigned int id;
  unsigned int size;
  float c[3];
  signed char flags;
  /** Name of the resource. (.hxc only?) */
  char name[HX_STRING_MAX_LENGTH];
} hx_wav_resource_object_t;

/**
 * WavResData:
 * A set of WaveFileIdObj links.
 */
typedef struct hx_wav_resource_data {
  hx_wav_resource_object_t res_data;
  /** Default link CUUID. If this exists,
   * there are usually no language links. */
  hx_cuuid_t default_cuuid;
  /** Number of language links. */
  unsigned int num_links;
  /* Language links to WaveFileIdObj entries  */
  HX_LINK(unsigned int language) links;
} hx_wav_resource_data_t;

/**
 * RandomResData:
 * Links to WavResData objects with probabilities of being played.
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
  HX_LINK(float probability) links;
} hx_random_resource_data_t;

/**
 * SwitchResData:
 * A kind of switch statement with cuuid links.
 */
typedef struct hx_switch_resource_data {
  unsigned int flag;
  unsigned int unknown;
  unsigned int unknown2;
  unsigned int start_index;
  unsigned int num_links;
  HX_LINK(unsigned int case_index) links;
} hx_switch_resource_data_t;

/**
 * ProgramResData:
 * An interpreted program with WavResData links.
 */
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

/**
 * WaveFileIdObj:
 * Holds references to an audio stream and its information.
 */
typedef struct hx_wave_file_id_object {
  /** Object pointer info */
  struct hx_id_object_pointer id_obj;
  /** Name of the stream */
  char name[HX_STRING_MAX_LENGTH];
  /** Filename of the external stream */
  char ext_stream_filename[HX_STRING_MAX_LENGTH];
  /** Size of the external stream */
  hx_size_t ext_stream_size;
  /** Offset in the external stream */
  hx_size_t ext_stream_offset;
  /** Audio stream */
  hx_audio_stream_t *audio_stream;
  /* internal */
  void* wave_header;
  void* extra_wave_data;
  hx_size_t extra_wave_data_length;
} hx_wave_file_id_object_t;

/**
 * Get the name of a class `c` for specified version `v`.
 * Returns the length of the output string.
 */
hx_size_t hx_class_name(enum hx_class c, enum hx_version v, char* buf, hx_size_t buf_sz);

#pragma mark - Entry

typedef struct hx_entry {
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
  HX_LINK(unsigned int code, unknown) language_links;
  
  /** File offset when writing */
  unsigned int file_offset;
  /** Entry size in bytes */
  unsigned int file_size;
  unsigned int tmp_file_size;
} hx_entry_t;

#undef HX_LINK

/**
 * Initialize an entry
 */
void hx_entry_init(hx_entry_t *e);

#pragma mark - Context

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
 * Get the current version of a context
 */
enum hx_version hx_context_version(const hx_t *hx);

/**
 * Get the number of entries in a context
 */
hx_size_t hx_context_num_entries(const hx_t *hx);

/**
 * Get an entry by index
 */
hx_entry_t *hx_context_get_entry(const hx_t *hx, hx_size_t index);

/**
 * Find entry by cuuid
 */
hx_entry_t *hx_context_find_entry(const hx_t *hx, hx_cuuid_t cuuid);

/**
 * Write context to file.
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
