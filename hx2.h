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

#define HX_INVALID_CUUID 0x0ull
#define HX_STRING_MAX_LENGTH 256

typedef unsigned int HX_Size;
/** A 64-bit unique identifier */
typedef unsigned long long HX_CUUID;
typedef char HX_String[HX_STRING_MAX_LENGTH];
typedef struct HX_Context HX_Context;

typedef char*(*HX_ReadCallback)(const char* filename, size_t pos, size_t *size, void* userdata);
typedef void (*HX_WriteCallback)(const char* filename, void* data, size_t pos, size_t *size, void* userdata);
typedef void (*HX_ErrorCallback)(const char* error_str, void* userdata);

enum HX_Version {
  HX_VERSION_HXD, /**< M/Arena */
  HX_VERSION_HXC, /**< R3 PC */
  HX_VERSION_HX2, /**< R3 PS2 */
  HX_VERSION_HXG, /**< R3 GCN */
  HX_VERSION_HXX, /**< R3 XBOX (+HD) */
  HX_VERSION_HX3, /**< R3 PS3 HD */
  HX_VERSION_INVALID,
};

enum HX_Language {
  HX_LANGUAGE_DE,
  HX_LANGUAGE_EN,
  HX_LANGUAGE_ES,
  HX_LANGUAGE_FR,
  HX_LANGUAGE_IT,
  HX_LANGUAGE_UNKNOWN,
};

#pragma mark - Audio

enum HX_AudioFormat {
  HX_AUDIO_FORMAT_PCM  = 0x01, /**< PCM s16 */
  HX_AUDIO_FORMAT_UBI  = 0x02, /**< UBI ADPCM */
  HX_AUDIO_FORMAT_PSX  = 0x03, /**< PS ADPCM */
  HX_AUDIO_FORMAT_DSP  = 0x04, /**< GC 4-bit ADPCM */
  HX_AUDIO_FORMAT_IMA  = 0x05, /**< MS IMA ADPCM */
  HX_AUDIO_FORMAT_MP3  = 0x55, /**< MPEG.3 */
};

typedef struct HX_AudioStream {
  struct HX_AudioStreamInfo {
    /**
     * Number of channels.
     */
    unsigned char num_channels;
    
    /**
     * Sample endianness
     */
    unsigned char endianness;
    
    /**
     * Sample rate. Usually 11025 or 22050 Hz
     */
    unsigned int sample_rate;
    
    /**
     * Sample count.
     */
    unsigned int num_samples;
    
    /**
     * Audio format
     */
    enum HX_AudioFormat fmt;
    
    /**
     * CUUID of the entry that contains this audio stream.
     */
    HX_CUUID wavefile_cuuid;
  } info;
  
  /**
   * Size of the audio data, in bytes.
   */
  HX_Size size;
  
  /**
   * Audio data.
   */
  signed short* data;
} HX_AudioStream;

/**
 * Get the name of an audio format.
 */
const char* hx_audio_format_name(const enum HX_AudioFormat);

void hx_audio_stream_init(HX_AudioStream *);
void hx_audio_stream_dealloc(HX_AudioStream *);

/**
 * Get the size of an audio stream.
 * @return Size of the stream in bytes.
 */
HX_Size hx_audio_stream_size(const HX_AudioStream *);

/**
 * Write audio stream to a waveformat file.
 * @param[in] stream Audio stream to be written
 * @return 1 on success.
 */
int hx_audio_stream_write_wav(const HX_Context *, HX_AudioStream *stream, const char* filename);

/**
 * Convert audio data.
 * The parameters of the desired output format should be set in the output stream info.
 * If the format of both the input and output stream is PCM, no conversion will be performed.
 * @param[in]     i_stream  Input audio stream
 * @param[in,out] o_stream  Output audio stream
 * @return 1 on success, 0 on encoding/decoding error, -1 on unsupported format.
 */
int hx_audio_convert(const HX_AudioStream *i_stream, HX_AudioStream *o_stream);


#pragma mark - Class -

#define HX_LINK(...) struct { HX_CUUID cuuid; __VA_ARGS__; } *

enum HX_Class {
  HX_CLASS_EVENT_RESOURCE_DATA,
  HX_CLASS_WAVE_RESOURCE_DATA,
  HX_CLASS_SWITCH_RESOURCE_DATA,
  HX_CLASS_RANDOM_RESOURCE_DATA,
  HX_CLASS_PROGRAM_RESOURCE_DATA,
  HX_CLASS_WAVE_FILE_ID_OBJECT,
  HX_CLASS_INVALID,
};

/**
 * EventResData
 * An event called by the game to start or stop audio playback.
 */
typedef struct EventResData {
  unsigned int type;
  /** The name of the event. Usually starts with 'Play_' or 'Stop_'. */
  char name[HX_STRING_MAX_LENGTH];
  /** Flags */
  unsigned int flags;
  /** The linked entry */
  HX_CUUID link;
  /** Unknown parameters. */
  float c[4];
} HX_EventResData;

/**
 * Superclass to WavResData.
 */
typedef struct WavResObj {
  unsigned int id;
  unsigned int size;
  float c[3];
  unsigned char flags;
  /** Name of the resource. (.hxc only?) */
  char name[HX_STRING_MAX_LENGTH];
} HX_WavResObj;

/**
 * A set of WaveFileIdObj links.
 */
typedef struct WavResData {
  HX_WavResObj res_data;
  /** Default link CUUID. */
  HX_CUUID default_cuuid;
  /** Number of language links. */
  unsigned int num_links;
  /* Language links to WaveFileIdObj entries  */
  HX_LINK(enum HX_Language language) links;
} HX_WavResData;

/**
 * A set of links to WavResData objects with probabilities of being played.
 */
typedef struct RandomResData {
  unsigned int flags;
  /** Unknown offset */
  float offset;
  /** The probability of not playing at all */
  float throw_probability;
  /** Number of CResData links */
  unsigned int num_links;
  /** ResData links */
  HX_LINK(float probability) links;
} HX_RandomResData;

/**
 * A switch statement of entry links.
 */
typedef struct SwitchResData {
  unsigned int flag;
  unsigned int unknown;
  unsigned int unknown2;
  unsigned int start_index;
  unsigned int num_links;
  HX_LINK(unsigned int case_index) links;
} HX_SwitchResData;

/**
 * An interpreted program with WavResData links.
 */
typedef struct ProgramResData {
  int num_links;
  HX_CUUID links[256];
  void* data;
} HX_ProgramResData;

/* the resource is located in an external file */
#define HX_ID_OBJ_PTR_FLAG_EXTERNAL (1 << 0)
/* the resource is located in the bigger file */
#define HX_ID_OBJ_PTR_FLAG_BIG_FILE (1 << 1)

/**
 * Location data for a resource.
 */
typedef struct IdObjPtr {
  unsigned int id;
  float unknown;
  unsigned int flags;
  unsigned int unknown2;
} HX_IdObjPtr;

/**
 * Holds references to an audio stream and its information.
 */
typedef struct WaveFileIdObj {
  /** Object pointer info */
  HX_IdObjPtr id_obj;
  /** Name of the stream */
  char name[HX_STRING_MAX_LENGTH];
  /** Filename of the external stream */
  char ext_stream_filename[HX_STRING_MAX_LENGTH];
  /** Size of the external stream */
  HX_Size ext_stream_size;
  /** Offset in the external stream */
  HX_Size ext_stream_offset;
  /** Audio stream */
  HX_AudioStream *audio_stream;
  
  /* private */
  void* _wave_header;
  void* _extra_wave_data;
  HX_Size _extra_wave_data_length;
} HX_WaveFileIdObj;

/**
 * Get the name of a class for a specific version.
 * @param[in]   i_class   Class to format into string
 * @param[in]   i_version Version to format the class for
 * @param[out]  buf       Output buffer
 * @param[in]   buf_sz    Length of the output buffer
 * @return The length of the output string.
 */
HX_Size hx_class_name(enum HX_Class i_class, enum HX_Version i_version, char* buf, HX_Size buf_sz);


#pragma mark - Entry

/**
 * HX index entry / class container.
 */
typedef struct HX_Entry {
  /**
   * The unique identifier of this entry.
   * May be shared across multiple context resource files.
   */
  HX_CUUID i_cuuid;
  
  /**
   * The class of the entry object.
   */
  enum HX_Class i_class;
  
  /**
   * Pointer to the class data.
   */
  void* p_data;
  
  /**
   * The number of linked CUUIDs.
   */
  HX_Size num_links;
  
  /**
   * Linked entry CUUIDs.
   */
  HX_CUUID* links;
  
  /**
   * Number of language links.
   */
  HX_Size num_languages;
  
  /**
   * Language links.
   */
  HX_LINK(enum HX_Language language, unknown) language_links;
  
  /* private */
  HX_Size _file_offset;
  HX_Size _file_size;
  HX_Size _tmp_file_size;
} HX_Entry;

void hx_entry_init(HX_Entry *);
void hx_entry_dealloc(HX_Entry *);


#pragma mark - Context

/**
 * Allocate an empty context.
 * @return Handle to the new context or NULL.
 */
HX_Context *hx_context_alloc(void);

/**
 * Set file i/o and error callbacks for the specified context.
 * @param[in] read  Read callback
 * @param[in] write Write callback
 * @param[in] error Error callback
 */
void hx_context_callback(HX_Context *, HX_ReadCallback read, HX_WriteCallback write, HX_ErrorCallback error, void* userdata);

/**
 * Load a .hx file.
 * @param[in] filename Filename with extension
 * @return 0 on success, -1 on failure.
 */
int hx_context_open(HX_Context *, const char* filename);

/**
 * Get current context version.
 */
enum HX_Version hx_context_version(const HX_Context *);

/**
 * Get context entry count.
 * @return The total number of entries in the context.
 */
HX_Size hx_context_num_entries(const HX_Context *);

/**
 * Get an entry by index.
 * @param[in] index A number less than the result of hx_context_num_entries
 * @return The entry at the specified index or NULL.
 */
HX_Entry *hx_context_get_entry(const HX_Context *, HX_Size index);

/**
 * Find an entry by CUUID.
 * @param[in] cuuid Unique 64-bit identifier
 * @return The entry with specified cuuid or NULL if not found.
 */
HX_Entry *hx_context_find_entry(const HX_Context *, HX_CUUID cuuid);

/**
 * Write context and resources to files.
 * @param[in] filename  Name of the .hx output file
 * @param[in] version   Desired version of the output context
 */
void hx_context_write(HX_Context *, const char* filename, enum HX_Version version);

/**
 * Free context data and all entries.
 */
void hx_context_free(HX_Context **);

#ifdef __cplusplus
}
#endif

#endif /* hx2_h */
