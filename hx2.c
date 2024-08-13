/*****************************************************************
 # hx2.c: Context implementation
 *****************************************************************
 * libhx2: library for reading and writing .hx audio files
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#include "hx2.h"
#include "stream.h"
#include "waveformat.h"

#include "codec.c"

struct HX_Context {
  enum HX_Version version;
  unsigned int index_offset;
  unsigned int index_type;
  unsigned int num_entries;
  HX_Entry* entries;
  
  stream_t stream;
  HX_ReadCallback read_cb;
  HX_WriteCallback write_cb;
  HX_ErrorCallback error_cb;
  void* userdata;
};

#define strlen(s) (HX_Size)strlen(s)

#pragma mark - Audio

void hx_audio_stream_init(HX_AudioStream *s) {
  s->info.fmt = HX_AUDIO_FORMAT_PCM;
  s->info.endianness = HX_NATIVE_ENDIAN;
  s->info.num_channels = 0;
  s->info.num_samples = 0;
  s->info.sample_rate = 0;
  s->info.wavefile_cuuid = 0;
  s->size = 0;
  s->data = NULL;
}

void hx_audio_stream_dealloc(HX_AudioStream *s) {
  free(s->data);
}

int hx_audio_stream_write_wav(const HX_Context *hx, HX_AudioStream *s, const char* filename) {
  struct waveformat_header header;
  waveformat_default_header(&header);
  header.sample_rate = s->info.sample_rate;
  header.num_channels = s->info.num_channels;
  header.bits_per_sample = 16;
  header.bytes_per_second = s->info.num_channels * s->info.sample_rate * header.bits_per_sample / 8;
  header.block_alignment = header.num_channels * header.bits_per_sample / 8;
  header.subchunk2_size = s->size;

  stream_t wave_stream = stream_alloc(s->size + sizeof(header), STREAM_MODE_WRITE, HX_LITTLE_ENDIAN);
  waveformat_rw(&wave_stream, &header, s->data);
  size_t sz = wave_stream.size;
  hx->write_cb(filename, wave_stream.buf, 0, &sz, hx->userdata);
  stream_dealloc(&wave_stream);
  
  return 0;
}

HX_Size hx_audio_stream_size(const HX_AudioStream *s) {
  switch (s->info.fmt) {
    case HX_AUDIO_FORMAT_PCM:
      return s->size;
    case HX_AUDIO_FORMAT_DSP:
      return dsp_pcm_size(HX_BYTESWAP32(*(unsigned*)s->data));
    default:
      return 0;
  }
}

int hx_audio_convert(const HX_AudioStream *in, HX_AudioStream *out) {
  if (in->info.fmt == HX_AUDIO_FORMAT_PCM && out->info.fmt == HX_AUDIO_FORMAT_PCM) { *out = *in; return 0; };
  if (in->info.fmt == HX_AUDIO_FORMAT_DSP && out->info.fmt == HX_AUDIO_FORMAT_PCM) return dsp_decode(in, out);
  if (in->info.fmt == HX_AUDIO_FORMAT_PSX && out->info.fmt == HX_AUDIO_FORMAT_PCM) return psx_decode(in, out);
  if (in->info.fmt == HX_AUDIO_FORMAT_PCM && out->info.fmt == HX_AUDIO_FORMAT_DSP) return dsp_encode(in, out);
  return -1;
}

#pragma mark -

static struct hx_class_table_entry {
  const char* name;
  int crossversion;
  int (*rw)(HX_Context*, HX_Entry*);
  void (*dealloc)(HX_Entry*);
} const hx_class_table[];

static struct hx_version_table_entry {
  const char* name;
  const char* platform;
  unsigned char endianness;
  unsigned int supported_codecs;
} const hx_version_table[] = {
  [HX_VERSION_HXD] = {"hxd", "PC", HX_BIG_ENDIAN, 0},
  [HX_VERSION_HXC] = {"hxc", "PC", HX_LITTLE_ENDIAN, HX_AUDIO_FORMAT_UBI | HX_AUDIO_FORMAT_PCM},
  [HX_VERSION_HX2] = {"hx2", "PS2", HX_LITTLE_ENDIAN, HX_AUDIO_FORMAT_PSX},
  [HX_VERSION_HXG] = {"hxg", "GC", HX_BIG_ENDIAN, HX_AUDIO_FORMAT_DSP},
  [HX_VERSION_HXX] = {"hxx", "XBox", HX_BIG_ENDIAN, 0},
  [HX_VERSION_HX3] = {"hx3", "PS3", HX_LITTLE_ENDIAN, 0},
};

static enum HX_Class hx_class_from_string(const char* name) {
  if (*name++ != 'C') return HX_CLASS_INVALID;
  if (!strncmp(name, "PC", 2)) name += 2;
  if (!strncmp(name, "GC", 2)) name += 2;
  if (!strncmp(name, "PS2", 3)) name += 3;
  if (!strncmp(name, "PS3", 3)) name += 3;
  if (!strncmp(name, "XBox", 4)) name += 4;
  if (!strncmp(name, "EventResData", 12)) return HX_CLASS_EVENT_RESOURCE_DATA;
  if (!strncmp(name, "WavResData", 10)) return HX_CLASS_WAVE_RESOURCE_DATA;
  if (!strncmp(name, "SwitchResData", 13)) return HX_CLASS_SWITCH_RESOURCE_DATA;
  if (!strncmp(name, "RandomResData", 13)) return HX_CLASS_RANDOM_RESOURCE_DATA;
  if (!strncmp(name, "ProgramResData", 14)) return HX_CLASS_PROGRAM_RESOURCE_DATA;
  if (!strncmp(name, "WaveFileIdObj", 13)) return HX_CLASS_WAVE_FILE_ID_OBJECT;
  return HX_CLASS_INVALID;
}

#pragma mark - Context

int hx_error(const HX_Context *hx, const char* format, ...) {
  va_list args;
  va_start(args, format);
  printf("[libhx] ");
  vfprintf(stderr, format, args);
  char buf[4096];
  vsnprintf(buf, 4096, format, args);
  hx->error_cb(buf, hx->userdata);
  printf("\n");
  va_end(args);
  return -1;
}

const char* hx_audio_format_name(enum HX_AudioFormat c) {
  if (c == HX_AUDIO_FORMAT_PCM) return "pcm";
  if (c == HX_AUDIO_FORMAT_UBI) return "ubi-adpcm";
  if (c == HX_AUDIO_FORMAT_PSX) return "psx-adpcm";
  if (c == HX_AUDIO_FORMAT_DSP) return "dsp-adpcm";
  if (c == HX_AUDIO_FORMAT_IMA) return "ima-adpcm";
  if (c == HX_AUDIO_FORMAT_MP3) return "mp3";
  return "invalid-codec";
}

static const enum HX_Language hx_language_from_code(unsigned int code) {
  switch (code) {
    case 0x64652020: return HX_LANGUAGE_DE;
    case 0x656E2020: return HX_LANGUAGE_EN;
    case 0x65732020: return HX_LANGUAGE_ES;
    case 0x66722020: return HX_LANGUAGE_FR;
    case 0x69742020: return HX_LANGUAGE_IT;
    default: return HX_LANGUAGE_UNKNOWN;
  }
}

static const unsigned int hx_language_to_code(const enum HX_Language language) {
  switch (language) {
    case HX_LANGUAGE_DE: return 0x64652020;
    case HX_LANGUAGE_EN: return 0x656E2020;
    case HX_LANGUAGE_ES: return 0x65732020;
    case HX_LANGUAGE_FR: return 0x66722020;
    case HX_LANGUAGE_IT: return 0x69742020;
    default: return 0;
  }
}

static const char *const hx_language_name(const enum HX_Language language) {
  switch (language) {
    case HX_LANGUAGE_DE: return "DE";
    case HX_LANGUAGE_EN: return "EN";
    case HX_LANGUAGE_ES: return "ES";
    case HX_LANGUAGE_FR: return "FR";
    case HX_LANGUAGE_IT: return "IT";
    default: return "Unknown Language";
  }
}

HX_Size hx_class_name(enum HX_Class class, enum HX_Version version, char* buf, HX_Size buf_sz) {
  const struct hx_version_table_entry v = hx_version_table[version];
  const struct hx_class_table_entry c = hx_class_table[class];
  return snprintf(buf, buf_sz, "C%s%s", c.crossversion ? "" : v.platform,  c.name);
}

enum HX_Version hx_context_version(const HX_Context *hx) {
  return hx->version;
}

HX_Size hx_context_num_entries(const HX_Context *hx) {
  return hx->num_entries;
}

HX_Entry* hx_context_get_entry(const HX_Context *hx, unsigned int index) {
  return (index < hx->num_entries) ? &hx->entries[index] : NULL;
}

HX_Entry *hx_context_find_entry(const HX_Context *hx, HX_CUUID cuuid) {
  for (unsigned int i = 0; i < hx->num_entries; i++)
    if (hx->entries[i].i_cuuid == cuuid) return &hx->entries[i];
  return NULL;
}

#pragma mark - Class -

#define hx_entry_data() \
  (entry->p_data = (hx->stream.mode == STREAM_MODE_WRITE ? entry->p_data : malloc(sizeof(*data))))

static int EventResData(HX_Context *hx, HX_Entry *entry) {
  HX_EventResData *data = hx_entry_data();
  unsigned int name_length = strlen(data->name);
  stream_rw32(&hx->stream, &data->type);
  stream_rw32(&hx->stream, &name_length);
  stream_rw(&hx->stream, &data->name, name_length);
  stream_rw32(&hx->stream, &data->flags);
  stream_rwcuuid(&hx->stream, &data->link);
  stream_rwfloat(&hx->stream, data->c + 0);
  stream_rwfloat(&hx->stream, data->c + 1);
  stream_rwfloat(&hx->stream, data->c + 2);
  stream_rwfloat(&hx->stream, data->c + 3);
  return 0;
}

static void EventResData_Free(HX_Entry *entry) {
  free(entry->p_data);
}

static void WavResObj(HX_Context *hx, HX_WavResObj *data) {
  stream_t *s = &hx->stream;
  stream_rw32(s, &data->id);
  
  if (hx->version == HX_VERSION_HXC) {
    unsigned int name_length = strlen(data->name);
    stream_rw32(s, &name_length);
    stream_rw(s, &data->name, name_length);
  }
  
  if (hx->version == HX_VERSION_HXG || hx->version == HX_VERSION_HX2) {
    memset(data->name, 0, HX_STRING_MAX_LENGTH);
    stream_rw32(s, &data->size);
  }
  
  stream_rwfloat(s, data->c + 0);
  stream_rwfloat(s, data->c + 1);
  stream_rwfloat(s, data->c + 2);
  stream_rw8(s, &data->flags);
}

static int WavResData(HX_Context *hx, HX_Entry *entry) {
  HX_WavResData *data = hx_entry_data();
  WavResObj(hx, &data->res_data);
  /* number of links are 0 by default */
  if (hx->stream.mode == STREAM_MODE_READ) {
    data->num_links = 0;
  }
  
  stream_rwcuuid(&hx->stream, &data->default_cuuid);
  
  // 1C = 1, 18 = 1
  // TODO: Figure out the rest of the flags
  if (data->res_data.flags & HX_WAVRES_OBJ_FLAG_MULTIPLE) {    
    stream_rw32(&hx->stream, &data->num_links);
    if (hx->stream.mode == STREAM_MODE_READ) {
      data->links = malloc(sizeof(*data->links) * data->num_links);
    }
  }
  
  for (unsigned int i = 0; i < data->num_links; i++) {
    unsigned int language_code = hx_language_to_code(data->links[i].language);
    stream_rw32(&hx->stream, &language_code);
    stream_rwcuuid(&hx->stream, &data->links[i].cuuid);
    if (hx->stream.mode == STREAM_MODE_READ) data->links[i].language = hx_language_from_code(language_code);
  }
  
  return 0;
}

static void WavResData_Free(HX_Entry *entry) {
  HX_WavResData *data = entry->p_data;
  free(data->links);
  free(entry->p_data);
}

static int SwitchResData(HX_Context *hx, HX_Entry *entry) {
  HX_SwitchResData *data = hx_entry_data();
  stream_rw32(&hx->stream, &data->flag);
  stream_rw32(&hx->stream, &data->unknown);
  stream_rw32(&hx->stream, &data->unknown2);
  stream_rw32(&hx->stream, &data->start_index);
  stream_rw32(&hx->stream, &data->num_links);
  
  if (hx->stream.mode == STREAM_MODE_READ) {
    data->links = malloc(sizeof(HX_SwitchResData) * data->num_links);
  }
  
  for (unsigned int i = 0; i < data->num_links; i++) {
    stream_rw32(&hx->stream, &data->links[i].case_index);
    stream_rwcuuid(&hx->stream, &data->links[i].cuuid);
  }
  
  return 0;
}

static void SwitchResData_Free(HX_Entry *entry) {
  HX_SwitchResData *data = entry->p_data;
  free(data->links);
  free(entry->p_data);
}

static int RandomResData(HX_Context *hx, HX_Entry *entry) {
  HX_RandomResData *data = hx_entry_data();
  stream_rw32(&hx->stream, &data->flags);
  stream_rwfloat(&hx->stream, &data->offset);
  stream_rwfloat(&hx->stream, &data->throw_probability);
  stream_rw32(&hx->stream, &data->num_links);
  
  if (hx->stream.mode == STREAM_MODE_READ) {
    data->links = malloc(sizeof(*data->links) * data->num_links);
  }
  
  for (unsigned i = 0; i < data->num_links; i++) {
    stream_rwfloat(&hx->stream, &data->links[i].probability);
    stream_rwcuuid(&hx->stream, &data->links[i].cuuid);
  }
  
  return 0;
}

static void RandomResData_Free(HX_Entry *entry) {
  HX_RandomResData *data = entry->p_data;
  free(data->links);
  free(entry->p_data);
}

static int ProgramResData(HX_Context *hx, HX_Entry *entry) {
  HX_ProgramResData *data = hx_entry_data();
  stream_t *s = &hx->stream;
  
  unsigned int pos = s->pos;
  
  char name[HX_STRING_MAX_LENGTH];
  unsigned int length = hx_class_name(entry->i_class, hx->version, name, HX_STRING_MAX_LENGTH);
  
  if (s->mode == STREAM_MODE_READ) {
    entry->_tmp_file_size = entry->_file_size - (4 + length + 8);
    data->data = malloc(entry->_file_size);
  }
  
  /* just copy the entire internal entry (minus the header) */
  stream_rw(s, data->data, s->mode == STREAM_MODE_READ ? entry->_file_size : entry->_tmp_file_size);
  
  /* lazy method: scan the buffer for the entries, which are just assumed to be in the correct order */
  if (s->mode == STREAM_MODE_READ) {
    data->num_links = 0;
    for (int i = 0; i < entry->_file_size; i++) {
      char* p = (char*)data->data + i;
      if (*p == 'E') {
        if (hx->version == HX_VERSION_HXC) p++;
        stream_t s = stream_create(++p, sizeof(HX_CUUID), STREAM_MODE_READ, hx->stream.endianness);
        HX_CUUID cuuid;
        stream_rwcuuid(&s, &cuuid);
        
        if (hx->version == HX_VERSION_HX2) {
          HX_CUUID tmp = HX_BYTESWAP32((unsigned)((cuuid & 0xFFFFFFFF00000000) >> 32));
          cuuid = HX_BYTESWAP32((unsigned)(cuuid & 0xFFFFFFFF)) | (tmp << 32);
        }
        
        unsigned int c = (cuuid & 0xFFFFFFFF00000000) >> 32;
        if (c == 3) {
          data->links[data->num_links++] = cuuid;
        }
      }
    }
  }
  
  return 0;
}

static void ProgramResData_Free(HX_Entry *entry) {
  HX_ProgramResData *data = entry->p_data;
  free(data->data);
  free(entry->p_data);
}

static void IdObjPtrRW(HX_Context *hx, HX_IdObjPtr *data) {
  stream_t *s = &hx->stream;
  stream_rw32(s, &data->id);
  stream_rwfloat(s, &data->unknown);
  if (hx->version == HX_VERSION_HXG) {
    stream_rw32(s, &data->flags);
    stream_rw32(s, &data->unknown2);
  } else {
    unsigned char tmp_flags = data->flags;
    stream_rw8(s, &tmp_flags);
    data->flags = tmp_flags;
  }
}

static int WaveFileIdObj(HX_Context *hx, HX_Entry *entry) {
  HX_WaveFileIdObj *data = hx_entry_data();
  IdObjPtrRW(hx, &data->id_obj);
  
  if (data->id_obj.flags & HX_ID_OBJ_PTR_FLAG_EXTERNAL) {
    if (hx->version == HX_VERSION_HX2) {
      memmove(data->ext_stream_filename + 2, data->ext_stream_filename, strlen(data->ext_stream_filename)+1);
      memcpy(data->ext_stream_filename, ".\\", 2);
    }
    unsigned int name_length = strlen(data->ext_stream_filename);
    stream_rw32(&hx->stream, &name_length);
    stream_rw(&hx->stream, data->ext_stream_filename, name_length);
  } else {
    data->ext_stream_offset = 0;
    data->ext_stream_size = 0;
  }
  
  if (hx->stream.mode == STREAM_MODE_READ) data->_wave_header = malloc(sizeof(struct waveformat_header));
  if (!waveformat_header_rw(&hx->stream, data->_wave_header)) {
    return hx_error(hx, "failed to read wave format header");
  }
  
  HX_AudioStream *audio_stream = (hx->stream.mode == STREAM_MODE_READ) ? malloc(sizeof(*audio_stream)) : data->audio_stream;
  if (hx->stream.mode == STREAM_MODE_READ) {
    struct waveformat_header* wave_header = data->_wave_header;
    audio_stream->info.fmt = wave_header->format;
    audio_stream->info.num_channels = wave_header->num_channels;
    audio_stream->info.endianness = hx->stream.endianness;
    audio_stream->info.sample_rate = wave_header->sample_rate;
    audio_stream->size = wave_header->subchunk2_size;
  }
  
  data->audio_stream = audio_stream;
  data->audio_stream->info.wavefile_cuuid = entry->i_cuuid;
  
  if (data->id_obj.flags & HX_ID_OBJ_PTR_FLAG_EXTERNAL) {
    struct waveformat_header* wave_header = data->_wave_header;
    /* data code must be "datx" */
    assert(wave_header->subchunk2_id == 0x78746164);
    /* the internal data length must be 8 */
    assert(wave_header->subchunk2_size == 8);
    
    stream_rw32(&hx->stream, &data->ext_stream_size);
    stream_rw32(&hx->stream, &data->ext_stream_offset);
    
    if (hx->stream.mode == STREAM_MODE_READ) {
      audio_stream->size = data->ext_stream_size;
      /* Make sure the filename is correctly formatted */
      if (!strncmp(data->ext_stream_filename, ".\\", 2)) {
        snprintf(data->ext_stream_filename, sizeof data->ext_stream_filename, "%s", data->ext_stream_filename + 2);
      }
        
      size_t sz = data->ext_stream_size;
      if (!(audio_stream->data = (short*)hx->read_cb(data->ext_stream_filename, data->ext_stream_offset, &sz, hx->userdata))) {
        return hx_error(hx, "failed to read from external stream (%s @ 0x%X)", data->ext_stream_filename, data->ext_stream_offset);
      }
    } else if (hx->stream.mode == STREAM_MODE_WRITE) {
      
      size_t sz = data->ext_stream_size;
      hx->write_cb(data->ext_stream_filename, data->audio_stream->data, data->ext_stream_offset, &sz, hx->userdata);
    }
  } else {
    struct waveformat_header* wave_header = data->_wave_header;
    /* data code must be "data" */
    assert(wave_header->subchunk2_id == 0x61746164);
    /* read internal stream data */
    audio_stream->data = (hx->stream.mode == STREAM_MODE_READ) ? malloc(wave_header->subchunk2_size) : audio_stream->data;
    stream_rw(&hx->stream, audio_stream->data, wave_header->subchunk2_size);
  }
  
  if (hx->stream.mode == STREAM_MODE_READ) {
    struct waveformat_header* wave_header = data->_wave_header;
    /* Temporary solution: determine the length of the
     * rest of the file and copy the data to a buffer.
     * TODO: Read more wave format chunk types */
    data->_extra_wave_data_length = (wave_header->riff_length + 8) - wave_header->subchunk2_size - sizeof(struct waveformat_header);
    data->_extra_wave_data = NULL;
    if (data->id_obj.flags & HX_ID_OBJ_PTR_FLAG_EXTERNAL)
      data->_extra_wave_data_length += 4;
    if (data->_extra_wave_data_length > 0) {
      if (!(data->id_obj.flags & HX_ID_OBJ_PTR_FLAG_EXTERNAL)) data->_extra_wave_data_length += 1;
      
      data->_extra_wave_data = malloc(data->_extra_wave_data_length);
      memcpy(data->_extra_wave_data, hx->stream.buf + hx->stream.pos, data->_extra_wave_data_length);
      stream_advance(&hx->stream, data->_extra_wave_data_length);
    }
  } else if (hx->stream.mode == STREAM_MODE_WRITE) {
    if (data->_extra_wave_data) {
      stream_rw(&hx->stream, data->_extra_wave_data, data->_extra_wave_data_length);
    }
    /* ? */
    if (hx->version == HX_VERSION_HX2 && hx->stream.mode == STREAM_MODE_WRITE) {
      stream_rw32(&hx->stream, &data->ext_stream_offset);
    }
  }
  
  return 0;
}

static void WaveFileIdObj_Free(HX_Entry *entry) {
  HX_WaveFileIdObj *data = entry->p_data;
  hx_audio_stream_dealloc(data->audio_stream);
  if (data->_extra_wave_data) free(data->_extra_wave_data);
  free(data->audio_stream);
  free(data->_wave_header);
  free(entry->p_data);
}

static const struct hx_class_table_entry hx_class_table[] = {
  [HX_CLASS_EVENT_RESOURCE_DATA] = {"EventResData", 1, EventResData, EventResData_Free},
  [HX_CLASS_WAVE_RESOURCE_DATA] = {"WavResData", 0, WavResData, WavResData_Free},
  [HX_CLASS_SWITCH_RESOURCE_DATA] = {"SwitchResData", 1, SwitchResData, SwitchResData_Free},
  [HX_CLASS_RANDOM_RESOURCE_DATA] = {"RandomResData", 1, RandomResData, RandomResData_Free},
  [HX_CLASS_PROGRAM_RESOURCE_DATA] = {"ProgramResData", 1, ProgramResData, ProgramResData_Free},
  [HX_CLASS_WAVE_FILE_ID_OBJECT] = {"WaveFileIdObj", 0, WaveFileIdObj, WaveFileIdObj_Free},
};

#pragma mark - Entry

void hx_entry_init(HX_Entry *e) {
  e->i_cuuid = HX_INVALID_CUUID;
  e->i_class = HX_CLASS_INVALID;
  e->num_links = 0;
  e->num_languages = 0;
  e->language_links = NULL;
  e->_file_offset = 0;
  e->_file_size = 0;
  e->_tmp_file_size = 0;
  e->p_data = NULL;
}

void hx_entry_dealloc(HX_Entry *e) {
  if (e->links) free(e->links);
  if (e->language_links) free(e->language_links);
  if (e->i_class != HX_CLASS_INVALID) hx_class_table[e->i_class].dealloc(e);
}

static int hx_entry_rw(HX_Context *hx, HX_Entry *entry) {
  int p = hx->stream.pos;
  
  char classname[HX_STRING_MAX_LENGTH];
  memset(classname, 0, HX_STRING_MAX_LENGTH);
  unsigned int classname_length = 0;
  if (hx->stream.mode == STREAM_MODE_WRITE) classname_length = hx_class_name(entry->i_class, hx->version, classname, HX_STRING_MAX_LENGTH);
  stream_rw32(&hx->stream, &classname_length);
  
  if (hx->stream.mode == STREAM_MODE_READ) memset(classname, 0, classname_length + 1);
  stream_rw(&hx->stream, classname, classname_length);
  
  if (hx->stream.mode == STREAM_MODE_READ) {
    enum HX_Class hclass = hx_class_from_string(classname);
    if (hclass != entry->i_class) {
      return hx_error(hx, "header class name does not match index class name (%X != %X)\n", entry->i_class, hclass);
    }
  }
  
  unsigned long long cuuid = entry->i_cuuid;
  stream_rwcuuid(&hx->stream, &cuuid);
  if (cuuid != entry->i_cuuid) {
    return hx_error(hx, "header cuuid does not match index cuuid (%016llX != %016llX)\n", entry->i_cuuid, cuuid);
  }
  
  if (entry->i_class != HX_CLASS_INVALID) {
    if (hx_class_table[entry->i_class].rw(hx, entry) != 0) {
      return hx_error(hx, "invalid class '%d'\n", entry->i_class);
    }
  }
  
  return (hx->stream.pos - p);
}

#pragma mark - HX

static void PostRead(HX_Context *hx) {
  if (hx->version == HX_VERSION_HXG) {
    /* In hxg, the WavResObj class has no internal name, so
     * derive them from the EventResData entries instead. */
    for (unsigned int i = 0; i < hx->num_entries; i++) {
      if (hx->entries[i].i_class == HX_CLASS_EVENT_RESOURCE_DATA) {
        HX_EventResData *data = hx->entries[i].p_data;
        HX_Entry *entry = hx_context_find_entry(hx, data->link);
        if (entry->i_class == HX_CLASS_WAVE_RESOURCE_DATA) {
          HX_WavResData *wavresdata = entry->p_data;
          strncpy(wavresdata->res_data.name, data->name, HX_STRING_MAX_LENGTH);
        }
      }
    }
  }
  
  for (unsigned int i = 0; i < hx->num_entries; i++) {
    if (hx->entries[i].i_class == HX_CLASS_WAVE_RESOURCE_DATA) {
      HX_WavResData *data = hx->entries[i].p_data;
      for (unsigned int l = 0; l < data->num_links; l++) {
        HX_WaveFileIdObj *obj = hx_context_find_entry(hx, data->links[l].cuuid)->p_data;
        
        char buf[HX_STRING_MAX_LENGTH];
        memset(buf, 0, HX_STRING_MAX_LENGTH);
        snprintf(buf, HX_STRING_MAX_LENGTH, "%s_%s", data->res_data.name, hx_language_name(data->links[l].language));
        memcpy(obj->name, buf, HX_STRING_MAX_LENGTH);
      }
    }
  }
}

static int HX2(HX_Context *hx) {
  /* initial definitions */
  unsigned int index_offset = 0;
  unsigned int index_code = 0x58444E49; /* "INDX" */
  unsigned int index_type = 2;
  unsigned int num_entries = hx->num_entries;
  
  stream_t index_stream = hx->stream;
  
  if (hx->stream.mode == STREAM_MODE_WRITE) {
    /* 255 bytes should be enough of an average size for each entry */
    index_stream = stream_alloc(hx->num_entries * 0xFF, STREAM_MODE_WRITE, hx->stream.endianness);
    /* reserve space for the index offset */
    stream_advance(&hx->stream, 4);
  } else if (hx->stream.mode == STREAM_MODE_READ) {
    /* read and seek to the index offset */
    stream_rw32(&index_stream, &index_offset);
    stream_seek(&index_stream, index_offset);
  }
  
  stream_rw32(&index_stream, &index_code);
  stream_rw32(&index_stream, &index_type);
  stream_rw32(&index_stream, &num_entries);
  
  if (index_code != 0x58444E49) {
    if (hx->stream.mode == STREAM_MODE_WRITE) stream_dealloc(&index_stream);
    return hx_error(hx, "invalid index header");
  }
  
  if (index_type != 0x1 && index_type != 0x2) {
    if (hx->stream.mode == STREAM_MODE_WRITE) stream_dealloc(&index_stream);
    return hx_error(hx, "invalid index type");
  }
  
  if (num_entries == 0) {
    if (hx->stream.mode == STREAM_MODE_WRITE) stream_dealloc(&index_stream);
    return hx_error(hx, "file contains no entries\n");
  }
  
  if (hx->stream.mode == STREAM_MODE_READ) {
    /* (re)allocate entry data */
    hx->num_entries += num_entries;
    hx->entries = realloc(hx->entries, sizeof(HX_Entry) * hx->num_entries);
  }
  
  while (num_entries--) {
    HX_Size classname_length = 0;
    HX_Entry *entry = &hx->entries[hx->num_entries - num_entries - 1];
    
    char classname[HX_STRING_MAX_LENGTH];
    if (index_stream.mode == STREAM_MODE_WRITE) {
      classname_length = hx_class_name(entry->i_class, hx->version, classname, HX_STRING_MAX_LENGTH);
    }
    
    stream_rw32(&index_stream, &classname_length);
    stream_rw(&index_stream, classname, classname_length);
    
    if (index_stream.mode == STREAM_MODE_READ) {
      hx_entry_init(entry);
      entry->i_class = hx_class_from_string(classname);
    }
      
    unsigned int zero = 0;
    stream_rwcuuid(&index_stream, &entry->i_cuuid);
    stream_rw32(&index_stream, &entry->_file_offset);
    stream_rw32(&index_stream, &entry->_file_size);
    stream_rw32(&index_stream, &zero);
    stream_rw32(&index_stream, &entry->num_links);
    
    assert(zero == 0);
    
    if (index_type == 0x2) {
      if (index_stream.mode == STREAM_MODE_READ) {
        entry->links = malloc(sizeof(*entry->links) * entry->num_links);
      }

      for (int i = 0; i < entry->num_links; i++) {
        stream_rwcuuid(&index_stream, entry->links + i);
      }

      stream_rw32(&index_stream, &entry->num_languages);
      if (index_stream.mode == STREAM_MODE_READ) {
        entry->language_links = malloc(sizeof(*entry->language_links) * entry->num_languages);
      }

      for (int i = 0; i < entry->num_languages; i++) {
        unsigned int language_code = hx_language_to_code(entry->language_links[i].language);
        stream_rw32(&index_stream, &language_code);
        stream_rw32(&index_stream, &entry->language_links[i].unknown);
        stream_rwcuuid(&index_stream, &entry->language_links[i].cuuid);
        if (index_stream.mode == STREAM_MODE_READ)
          entry->language_links[i].language = hx_language_from_code(language_code);
      }
    }
    
    if (hx->stream.mode == STREAM_MODE_READ) {
      stream_seek(&hx->stream, entry->_file_offset);
    }
    
    if (hx_entry_rw(hx, entry) <= 0) {
      hx_error(hx, "failed to %s entry %016llX", index_stream.mode == STREAM_MODE_READ ? "read" : "write", entry->i_cuuid);
    }
  }
  
  if (hx->stream.mode == STREAM_MODE_WRITE) {
    /* Copy the index to the end of the file */
    unsigned int index_size = index_stream.pos;
    index_offset = hx->stream.pos;
    stream_rw(&hx->stream, index_stream.buf, index_size);
    
    hx->stream.size = hx->stream.pos;
    if (hx->version == HX_VERSION_HXG || hx->version == HX_VERSION_HX2) {
      hx->stream.size += (8 * 4);
      memset(hx->stream.buf + hx->stream.pos, 0, 8 * 4);
    }
    
    /* write info */
    int sz = sprintf(hx->stream.buf + hx->stream.pos, "This file was written by libhx2.");
    sz += (16 - (hx->stream.pos + sz) % 16) % 16;
    hx->stream.size = hx->stream.pos + sz;
    stream_advance(&hx->stream, sz);
    
    stream_seek(&hx->stream, 0);
    stream_rw32(&hx->stream, &index_offset);
    stream_dealloc(&index_stream);
  }

  return 0;
}

HX_Context *hx_context_alloc() {
  HX_Context *hx = malloc(sizeof(*hx));
  hx->version = HX_VERSION_INVALID;
  hx->entries = NULL;
  hx->num_entries = 0;
  return hx;
}

void hx_context_callback(HX_Context *hx, HX_ReadCallback read, HX_WriteCallback write, HX_ErrorCallback error, void* userdata) {
  hx->read_cb = read;
  hx->write_cb = write;
  hx->error_cb = error;
  hx->userdata = userdata;
}

int hx_context_open(HX_Context *hx, const char* filename) {
  if (!filename) {
    return hx_error(hx, "invalid filename", filename);
  }
  
  const char* ext = strrchr(filename, '.');
  if (ext) {
    for (int v = 0; v < sizeof(hx_version_table) / sizeof(*hx_version_table); v++) {
      if (strcasecmp(ext+1, hx_version_table[v].name) == 0) {
        hx->version = v;
        break;
      }
    }
  } else {
  }
  
  size_t size = SIZE_MAX;
  char* data = hx->read_cb(filename, 0, &size, hx->userdata);
  if (!data) {
    return hx_error(hx, "failed to read %s", filename);
  }
  
  if (hx->version == HX_VERSION_INVALID) {
    return hx_error(hx, "invalid hx file version");
  }
  
  stream_t stream = stream_create(data, size, STREAM_MODE_READ, hx_version_table[hx->version].endianness);
  hx->stream = stream;
  
  if (HX2(hx) != 0)
    goto fail;
  return 0;
fail:
  free(data);
  return -1;
}

void hx_context_write(HX_Context *hx, const char* filename, enum HX_Version version) {
  stream_t ps = hx->stream;
  hx->stream = stream_alloc(0x4FFFFF, STREAM_MODE_WRITE, ps.endianness);
  memset(hx->stream.buf, 0, hx->stream.size);
  
  hx->version = version;
  if (HX2(hx) != 0) return;
  
  size_t size = hx->stream.size;
  hx->write_cb(filename, hx->stream.buf, 0, &size, hx->userdata);
  stream_dealloc(&hx->stream);
  hx->stream = ps;
}

void hx_context_free(HX_Context **hx) {
  for (unsigned int i = 0; i < (*hx)->num_entries; i++) {
    HX_Entry *e = (*hx)->entries + i;
    hx_entry_dealloc(e);
  }
  free((*hx)->entries);
  free(*hx);
}
