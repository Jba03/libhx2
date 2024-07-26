/*****************************************************************
 # hx2.c: Context implementation
 *****************************************************************
 * libhx2: library for reading and writing .hx audio files
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#include "hx2.h"
#include "stream.h"
#include "waveformat.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

struct hx {
  hx_entry_t* entries;
  
  unsigned int index_offset;
  unsigned int index_type;
  unsigned int num_entries;
  
  enum hx_version version;
  /* read/write stream */
  hx_stream_t stream;
  /* callbacks + userdata */
  hx_read_callback_t read_cb;
  hx_write_callback_t write_cb;
  void* userdata;
};

static struct hx_class_table_entry {
  const char* name;
  int crossversion;
  int (*rw)(hx_t*, hx_entry_t*);
  void (*dealloc)(hx_t*, hx_entry_t*);
} const hx_class_table[];

static struct hx_version_table_entry {
  const char* name;
  const char* platform;
  unsigned char endianness;
  unsigned int supported_codecs;
} const hx_version_table[] = {
  [HX_VERSION_HXD] = {"hxd", "PC", HX_BIG_ENDIAN, 0},
  [HX_VERSION_HXC] = {"hxc", "PC", HX_LITTLE_ENDIAN, HX_CODEC_UBI | HX_CODEC_PCM},
  [HX_VERSION_HX2] = {"hx2", "PS2", HX_LITTLE_ENDIAN, HX_CODEC_PSX},
  [HX_VERSION_HXG] = {"hxg", "GC", HX_BIG_ENDIAN, HX_CODEC_DSP},
  [HX_VERSION_HXX] = {"hxx", "XBox", HX_BIG_ENDIAN, 0},
  [HX_VERSION_HX3] = {"hx3", "PS3", HX_LITTLE_ENDIAN, 0},
};

static enum hx_class hx_class_from_string(const char* name) {
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

void hx_class_to_string(hx_t *hx, enum hx_class class, char *out, unsigned int *out_sz) {
  const struct hx_version_table_entry v = hx_version_table[hx->version];
  const struct hx_class_table_entry c = hx_class_table[class];
  int q = sprintf(out, "C%s%s", c.crossversion ? "" : v.platform,  c.name);
  if (out_sz) *out_sz += q;
}

void hx_context_get_entries(hx_t *hx, hx_entry_t** entries, int *count) {
  *entries = hx->entries;
  *count = hx->num_entries;
}

hx_entry_t *hx_context_entry_lookup(hx_t *hx, unsigned long long cuuid) {
  for (unsigned int i = 0; i < hx->num_entries; i++)
    if (hx->entries[i].cuuid == cuuid) return &hx->entries[i];
  return NULL;
}

int hx_error(hx_t *hx, const char* format, ...) {
  va_list args;
  va_start(args, format);
  printf("[libhx] ");
  vprintf(format, args);
  printf("\n");
  va_end(args);
  return 0;
}

#pragma mark - Audio stream

int hx_audio_stream_write_wav(hx_t *hx, hx_audio_stream_t *s, const char* filename) {
  struct waveformat_header header;
  waveformat_default_header(&header);
  header.sample_rate = s->info.sample_rate;
  header.num_channels = s->info.num_channels;
  header.bits_per_sample = 16;
  header.bytes_per_second = s->info.num_channels * s->info.sample_rate * header.bits_per_sample / 8;
  header.block_alignment = header.num_channels * header.bits_per_sample / 8;
  header.subchunk2_size = s->size;

  hx_stream_t wave_stream = hx_stream_alloc(s->size + sizeof(header), HX_STREAM_MODE_WRITE, HX_LITTLE_ENDIAN);
  waveformat_rw(&wave_stream, &header, s->data);
  size_t sz = wave_stream.size;
  hx->write_cb(filename, wave_stream.buf, 0, &sz, hx->userdata);
  hx_stream_dealloc(&wave_stream);
  
  return 1;
}

unsigned int hx_audio_stream_size(hx_audio_stream_t *s) {
  switch (s->info.codec) {
    case HX_CODEC_PCM:
      return s->size;
    case HX_CODEC_DSP:
      return dsp_pcm_size(HX_BYTESWAP32(*(unsigned*)s->data));
    default:
      return 0;
  }
}

#define hx_entry_select() (hx->stream.mode == HX_STREAM_MODE_WRITE ? entry->data : malloc(sizeof(*data)))

static int hx_event_resource_data_rw(hx_t *hx, hx_entry_t *entry) {
  hx_event_resource_data_t *data = hx_entry_select();
  entry->data = data;
  
  unsigned int name_length = strlen(data->name);
  hx_stream_rw32(&hx->stream, &data->type);
  hx_stream_rw32(&hx->stream, &name_length);
  hx_stream_rw(&hx->stream, &data->name, name_length);
  hx_stream_rw32(&hx->stream, &data->flags);
  hx_stream_rwcuuid(&hx->stream, &data->link_cuuid);
  hx_stream_rwfloat(&hx->stream, data->f_param + 0);
  hx_stream_rwfloat(&hx->stream, data->f_param + 1);
  hx_stream_rwfloat(&hx->stream, data->f_param + 2);
  hx_stream_rwfloat(&hx->stream, data->f_param + 3);
  
  return 1;
}

static void hx_wav_resource_obj_rw(hx_t *hx, hx_wav_resource_object_t *data) {
  hx_stream_t *s = &hx->stream;
  hx_stream_rw32(s, &data->id);
  
  if (hx->version == HX_VERSION_HXC) {
    unsigned int name_length = strlen(data->name);
    hx_stream_rw32(s, &name_length);
    hx_stream_rw(s, &data->name, name_length);
  }
  
  if (hx->version == HX_VERSION_HXG) {
    memset(data->name, 0, HX_STRING_MAX_LENGTH);
    hx_stream_rw32(s, &data->size);
  }
  
  hx_stream_rwfloat(s, &data->c0);
  hx_stream_rwfloat(s, &data->c1);
  hx_stream_rwfloat(s, &data->c2);
  hx_stream_rw8(s, &data->flags);
}

#define HX_WAVRESDATA_FLAG_MULTIPLE (1 << 1)

static int hx_wave_resource_data_rw(hx_t *hx, hx_entry_t *entry) {
  hx_wav_resource_data_t *data = hx_entry_select();
  /* read parent class (WavResObj) */
  hx_wav_resource_obj_rw(hx, &data->res_data);
  /* number of links are 0 by default */
  if (hx->stream.mode == HX_STREAM_MODE_READ) {
    data->num_links = 0;
  }
  
  hx_stream_rwcuuid(&hx->stream, &data->default_cuuid);
  
  // 1C = 1, 18 = 1
  // TODO: Figure out the rest of the flags
  
  if (data->res_data.flags & HX_WAVRESDATA_FLAG_MULTIPLE) {
    /* default cuuid should be zero on GC */
    if (hx->version == HX_VERSION_HXG) {
      assert(data->default_cuuid == 0);
    }
    
    hx_stream_rw32(&hx->stream, &data->num_links);
    if (hx->stream.mode == HX_STREAM_MODE_READ) {
      data->links = malloc(sizeof(struct hx_wav_resource_data_link) * data->num_links);
    }
  }
  
  for (unsigned int i = 0; i < data->num_links; i++) {
    hx_stream_rw32(&hx->stream, &data->links[i].language);
    hx_stream_rwcuuid(&hx->stream, &data->links[i].cuuid);
   // unsigned int language_code = HX_BYTESWAP32(data->links[i].language);
  }
  
  entry->data = data;
  return 1;
}

static int hx_switch_resource_data_rw(hx_t *hx, hx_entry_t *entry) {
  hx_switch_resource_data_t *data = hx_entry_select();
  hx_stream_rw32(&hx->stream, &data->flag);
  hx_stream_rw32(&hx->stream, &data->unknown);
  hx_stream_rw32(&hx->stream, &data->unknown2);
  hx_stream_rw32(&hx->stream, &data->start_index);
  hx_stream_rw32(&hx->stream, &data->num_links);
  
  if (hx->stream.mode == HX_STREAM_MODE_READ) {
    data->links = malloc(sizeof(hx_switch_resource_data_t) * data->num_links);
  }
  
  for (unsigned int i = 0; i < data->num_links; i++) {
    hx_stream_rw32(&hx->stream, &data->links[i].case_index);
    hx_stream_rwcuuid(&hx->stream, &data->links[i].cuuid);
  }
}

static int hx_random_resource_data_rw(hx_t *hx, hx_entry_t *entry) {
  hx_random_resource_data_t *data = hx_entry_select();
  hx_stream_rw32(&hx->stream, &data->flags);
  hx_stream_rwfloat(&hx->stream, &data->offset);
  hx_stream_rwfloat(&hx->stream, &data->throw_probability);
  hx_stream_rw32(&hx->stream, &data->num_links);
  
  if (hx->stream.mode == HX_STREAM_MODE_READ) {
    data->links = malloc(sizeof(struct hx_random_resource_data_link) * data->num_links);
  }
  
  for (unsigned i = 0; i < data->num_links; i++) {
    hx_stream_rwfloat(&hx->stream, &data->links[i].probability);
    hx_stream_rwcuuid(&hx->stream, &data->links[i].cuuid);
  }
  
  entry->data = data;
  return 1;
}

#pragma mark - Class: ProgramResData

struct hx_class_program_resource_data { };

static int hx_program_resource_data_rw(hx_t *hx, hx_entry_t *entry) {
  struct hx_class_program_resource_data *data = hx_entry_select();
  hx_stream_t *s = &hx->stream;
  
  char name[256];
  unsigned int length = 0;
  hx_class_to_string(hx, entry->class, name, &length);
  
  if (s->mode == HX_STREAM_MODE_READ) {
    entry->tmp_file_size = entry->file_size - (4 + length + 8);
    entry->data = malloc(entry->file_size);
  }
  
  /* just copy the entire internal entry (minus the header) */
  hx_stream_rw(s, entry->data, s->mode == HX_STREAM_MODE_READ ? entry->file_size : entry->tmp_file_size);
  
  return 1;
}

#pragma mark - SuperClass: IdObjPtr

#define HX_ID_OBJECT_POINTER_FLAG_EXTERNAL  (1 << 0)
#define HX_ID_OBJECT_POINTER_FLAG_BIG_FILE  (1 << 1)

static void hx_id_obj_pointer_rw(hx_t *hx, hx_id_object_pointer_t *data) {
  hx_stream_t *s = &hx->stream;
  hx_stream_rw32(s, &data->id);
  hx_stream_rwfloat(s, &data->unknown);
  if (hx->version == HX_VERSION_HXG) {
    hx_stream_rw32(s, &data->flags);
    hx_stream_rw32(s, &data->unknown2);
  } else {
    unsigned char tmp_flags;
    hx_stream_rw8(s, &tmp_flags);
    data->flags = tmp_flags;
  }
}

#pragma mark - Class: WaveFileIdObj

/* Read a WaveFileIdObj entry */
static int hx_wave_file_id_obj_rw(hx_t *hx, hx_entry_t *entry) {
  //hx_stream_t *s = &hx->stream;
  hx_wave_file_id_object_t *data = entry->data = hx_entry_select();
  hx_id_obj_pointer_rw(hx, &data->id_obj);
  
  if (data->id_obj.flags & HX_ID_OBJECT_POINTER_FLAG_EXTERNAL) {
    unsigned int name_length = strlen(data->ext_stream_filename);
    hx_stream_rw32(&hx->stream, &name_length);
    hx_stream_rw(&hx->stream, data->ext_stream_filename, name_length);
  }
  
  if (!waveformat_header_rw(&hx->stream, &data->wave_header)) {
    hx_error(hx, "failed to read wave format header");
    return 0;
  }
  
  hx_audio_stream_t *audio_stream = (hx->stream.mode == HX_STREAM_MODE_READ) ? malloc(sizeof(*audio_stream)) : data->audio_stream;
  if (hx->stream.mode == HX_STREAM_MODE_READ) {
    audio_stream->info.codec = data->wave_header.format;
    audio_stream->info.num_channels = data->wave_header.num_channels;
    audio_stream->info.endianness = hx->stream.endianness;
    audio_stream->info.sample_rate = data->wave_header.sample_rate;
    audio_stream->size = data->wave_header.subchunk2_size;
  }
  
  data->audio_stream = audio_stream;
  
  if (data->id_obj.flags & HX_ID_OBJECT_POINTER_FLAG_EXTERNAL) {
    /* data code must be "datx" */
    assert(data->wave_header.subchunk2_id == 0x78746164);
    /* the internal data length must be 8 */
    assert(data->wave_header.subchunk2_size == 8);
    
    hx_stream_rw32(&hx->stream, &data->ext_stream_size);
    hx_stream_rw32(&hx->stream, &data->ext_stream_offset);
    
    if (hx->stream.mode == HX_STREAM_MODE_READ) {
      audio_stream->size = data->ext_stream_size;
      /* Make sure the filename is correctly formatted */
      if (!strncmp(data->ext_stream_filename, ".\\", 2)) sprintf(data->ext_stream_filename, "%s", data->ext_stream_filename + 2);
      
      size_t sz = data->ext_stream_size;
      if (!(audio_stream->data = (int16_t*)hx->read_cb(data->ext_stream_filename, data->ext_stream_offset, &sz, hx->userdata))) {
        hx_error(hx, "failed to read external audio stream data (%s @ 0x%X)", data->ext_stream_filename, data->ext_stream_offset);
        return 0;
      }
    } else if (hx->stream.mode == HX_STREAM_MODE_WRITE) {
      
    }
  } else {
    /* data code must be "data" */
    assert(data->wave_header.subchunk2_id == 0x61746164);
    /* read internal stream data */
    audio_stream->data = (hx->stream.mode == HX_STREAM_MODE_READ) ? malloc(data->wave_header.subchunk2_size) : audio_stream->data;
    hx_stream_rw(&hx->stream, audio_stream->data, data->wave_header.subchunk2_size);
  }
  
  if (hx->stream.mode == HX_STREAM_MODE_READ) {
    /* Temporary solution: determine the length of the
     * rest of the file and copy the data to a buffer.
     * TODO: Read more wave format chunk types */
    data->extra_wave_data_length = (data->wave_header.riff_length + 8) - data->wave_header.subchunk2_size - sizeof(struct waveformat_header);
    data->extra_wave_data = NULL;
    if (data->id_obj.flags & HX_ID_OBJECT_POINTER_FLAG_EXTERNAL)
      data->extra_wave_data_length += 4;
    if (data->extra_wave_data_length > 0) {
      if (!(data->id_obj.flags & HX_ID_OBJECT_POINTER_FLAG_EXTERNAL)) data->extra_wave_data_length += 1;
      
      data->extra_wave_data = malloc(data->extra_wave_data_length);
      memcpy(data->extra_wave_data, hx->stream.buf + hx->stream.pos, data->extra_wave_data_length);
      hx_stream_advance(&hx->stream, data->extra_wave_data_length);
    }
  } else if (hx->stream.mode == HX_STREAM_MODE_WRITE) {
    if (data->extra_wave_data) hx_stream_rw(&hx->stream, data->extra_wave_data, data->extra_wave_data_length);
  }
  
  return 1;
}

static const struct hx_class_table_entry hx_class_table[] = {
  [HX_CLASS_EVENT_RESOURCE_DATA] = {"EventResData", 1, hx_event_resource_data_rw},
  [HX_CLASS_WAVE_RESOURCE_DATA] = {"WavResData", 0, hx_wave_resource_data_rw},
  [HX_CLASS_SWITCH_RESOURCE_DATA] = {"SwitchResData", 1, hx_switch_resource_data_rw},
  [HX_CLASS_RANDOM_RESOURCE_DATA] = {"RandomResData", 1, hx_random_resource_data_rw},
  [HX_CLASS_PROGRAM_RESOURCE_DATA] = {"ProgramResData", 1, hx_program_resource_data_rw},
  [HX_CLASS_WAVE_FILE_ID_OBJECT] = {"WaveFileIdObj", 0, hx_wave_file_id_obj_rw},
};

static int hx_entry_rw(hx_t *hx, hx_entry_t *entry) {
  int p = hx->stream.pos;
  
  char classname[256];
  memset(classname, 0, 256);
  unsigned int classname_length;
  if (hx->stream.mode == HX_STREAM_MODE_WRITE) hx_class_to_string(hx, entry->class, classname, &classname_length);
  hx_stream_rw32(&hx->stream, &classname_length);
  
  if (hx->stream.mode == HX_STREAM_MODE_READ) memset(classname, 0, classname_length + 1);
  hx_stream_rw(&hx->stream, classname, classname_length);
  
  if (hx->stream.mode == HX_STREAM_MODE_READ) {
    enum hx_class hclass = hx_class_from_string(classname);
    if (hclass != entry->class) {
      fprintf(stderr, "[libhx] header class name does not match index class name (%X != %X)\n", entry->class, hclass);
      return -1;
    }
  }
  
  unsigned long long cuuid = entry->cuuid;
  hx_stream_rwcuuid(&hx->stream, &cuuid);
  if (cuuid != entry->cuuid) {
    fprintf(stderr, "[libhx] header cuuid does not match index cuuid (%016llX != %016llX)\n", entry->cuuid, cuuid);
    return -1;
  }
  
  if (entry->class != HX_CLASS_INVALID) {
    if (!hx_class_table[entry->class].rw(hx, entry)) return -1;
  }
  
  return (hx->stream.pos - p);
}

static void hx_postread(hx_t *hx) {
  if (hx->version == HX_VERSION_HXG) {
    /* In hxg, the WavResObj class has no internal name, so
     * derive them from the EventResData entries instead. */
    for (unsigned int i = 0; i < hx->num_entries; i++) {
      if (hx->entries[i].class == HX_CLASS_EVENT_RESOURCE_DATA) {
        hx_event_resource_data_t *data = hx->entries[i].data;
        hx_entry_t *entry = hx_context_entry_lookup(hx, data->link_cuuid);
        if (entry->class == HX_CLASS_WAVE_RESOURCE_DATA) {
          hx_wav_resource_data_t *wavresdata = entry->data;
          strncpy(wavresdata->res_data.name, data->name, HX_STRING_MAX_LENGTH);
        }
      }
    }
  }
  
  for (unsigned int i = 0; i < hx->num_entries; i++) {
    if (hx->entries[i].class == HX_CLASS_WAVE_RESOURCE_DATA) {
      hx_wav_resource_data_t *data = hx->entries[i].data;
      for (unsigned int l = 0; l < data->num_links; l++) {
        hx_wav_resource_data_link_t *link = &data->links[l];
        hx_wave_file_id_object_t *obj = hx_context_entry_lookup(hx, link->cuuid)->data;
        
        unsigned int language = HX_BYTESWAP32(link->language);
        
        char buf[HX_STRING_MAX_LENGTH];
        memset(buf, 0, HX_STRING_MAX_LENGTH);
        snprintf(buf, HX_STRING_MAX_LENGTH, "%s_%.2s", data->res_data.name, (char*)&language);
        memcpy(obj->name, buf, HX_STRING_MAX_LENGTH);
      }
    }
  }
}

static int hx_read(hx_t *hx) {
  hx_stream_t *s = &hx->stream;
  unsigned int index_table_offset;
  unsigned int index_code;
  unsigned int index_type;
  unsigned int num_entries;
  
  hx_stream_rw32(s, &index_table_offset);
  hx_stream_seek(s, index_table_offset);
  hx_stream_rw32(s, &index_code);
  hx_stream_rw32(s, &index_type);
  hx_stream_rw32(s, &num_entries);
  
  if (index_code != 0x58444E49) {
    return hx_error(hx, "invalid index header");
  }
  
  if (index_type != 0x1 && index_type != 0x2) {
    return hx_error(hx, "invalid index type");
  }
  
  if (num_entries == 0) {
    return hx_error(hx, "file contains no entries\n");
  }
    
  hx->num_entries = num_entries;
  hx->entries = malloc(sizeof(hx_entry_t) * hx->num_entries);
  
  while (num_entries--) {
    unsigned int classname_length;
    hx_stream_rw32(s, &classname_length);
    
    char classname[classname_length + 1];
    memset(classname, 0, classname_length + 1);
    memcpy(classname, s->buf + s->pos, classname_length);
    hx_stream_advance(s, classname_length);
    
    hx_entry_t* entry = &hx->entries[hx->num_entries - num_entries - 1];
    entry->class = hx_class_from_string(classname);
    entry->num_links = 0;
    entry->num_languages = 0;
    entry->data = NULL;
    
    if (entry->class == HX_CLASS_INVALID) {
      fprintf(stderr, "found unknown class \"%s\": report this!\n", classname);
      continue;
    }
    
    unsigned int zero;
    hx_stream_rwcuuid(s, &entry->cuuid);
    hx_stream_rw32(s, &entry->file_offset);
    hx_stream_rw32(s, &entry->file_size);
    hx_stream_rw32(s, &zero);
    hx_stream_rw32(s, &entry->num_links);
    
    assert(zero == 0x00);
    
    if (index_type == 0x2) {
      entry->links = malloc(sizeof(*entry->links) * entry->num_links);
      for (int i = 0; i < entry->num_links; i++) {
        hx_stream_rwcuuid(s, entry->links + i);
      }

      hx_stream_rw32(s, &entry->num_languages);
      entry->language_links = malloc(sizeof(hx_entry_language_link_t) * entry->num_languages);
      
      for (int i = 0; i < entry->num_languages; i++) {
        hx_stream_rw32(s, &entry->language_links[i].code);
        hx_stream_rw32(s, &entry->language_links[i].unknown);
        hx_stream_rwcuuid(s, &entry->language_links[i].cuuid);
      }
    }

    //printf("[%d] %s (%016llX)\n", hx->num_entries - num_entries - 1, classname, entry->cuuid);
    
    unsigned int pos = s->pos;
    hx_stream_seek(s, entry->file_offset);
    hx_entry_rw(hx, entry);
    hx_stream_seek(s, pos);
  }
  
  hx_postread(hx);
  
  return 1;
}

static void hx_write(hx_t *hx) {
  hx_stream_t *s = &hx->stream;
  /* Allocate index stream */
  hx_stream_t index_stream = hx_stream_alloc(hx->num_entries * 0xFF, HX_STREAM_MODE_WRITE, s->endianness);
  /* Reserve space for index offset */
  s->pos += 4;
  
  unsigned int index_code = 0x58444E49; /* "INDX" */
  unsigned int index_type = 2;
  unsigned int num_entries = hx->num_entries;
  
  hx_stream_rw32(&index_stream, &index_code);
  hx_stream_rw32(&index_stream, &index_type);
  hx_stream_rw32(&index_stream, &num_entries);
  
  while (num_entries--) {
    hx_entry_t entry = hx->entries[hx->num_entries - num_entries - 1];
    
    char classname[256];
    unsigned int classname_length = 0;
    hx_class_to_string(hx, entry.class, classname, &classname_length);
    
    //printf("Writing [%d]: %s (%016llX)\n", hx->num_entries - num_entries - 1, classname, entry.cuuid);
    if (!hx_entry_rw(hx, &entry)) {
      return hx_error(hx, "failed to write entry %016llX", entry.cuuid);
    }

    hx_stream_rw32(&index_stream, &classname_length);
    hx_stream_rw(&index_stream, classname, classname_length);

    unsigned int zero = 0;
    hx_stream_rwcuuid(&index_stream, &entry.cuuid);
    hx_stream_rw32(&index_stream, &entry.file_offset);
    hx_stream_rw32(&index_stream, &entry.file_size);
    hx_stream_rw32(&index_stream, &zero);
    hx_stream_rw32(&index_stream, &entry.num_links);
    
    if (index_type == 0x2) {
      for (int i = 0; i < entry.num_links; i++) {
        hx_stream_rwcuuid(&index_stream, entry.links + i);
      }

      hx_stream_rw32(&index_stream, &entry.num_languages);
      for (int i = 0; i < entry.num_languages; i++) {
        hx_stream_rw32(&index_stream, &entry.language_links[i].code);
        hx_stream_rw32(&index_stream, &entry.language_links[i].unknown);
        hx_stream_rwcuuid(&index_stream, &entry.language_links[i].cuuid);
      }
    }
  }
  
  /* Copy the index to the end of the file */
  unsigned int index_size = index_stream.pos;
  unsigned int index_offset = s->pos;
  hx_stream_rw(s, index_stream.buf, index_size);
  
  s->size = s->pos + (8 * 4);
  memset(s->buf + s->pos, 0, 8 * 4);
  
  hx_stream_seek(s, 0);
  hx_stream_rw32(s, &index_offset);
  
  hx_stream_dealloc(&index_stream);

  return 0;
}

#pragma mark - HX

hx_t *hx_context_alloc() {
  hx_t *hx = malloc(sizeof(*hx));
  hx->version = HX_VERSION_INVALID;
  return hx;
}

void hx_context_callback(hx_t *hx, hx_read_callback_t read, hx_write_callback_t write, void* userdata) {
  hx->read_cb = read;
  hx->write_cb = write;
  hx->userdata = userdata;
}

int hx_context_open(hx_t *hx, const char* filename) {
  if (!filename) {
    hx_error(hx, "invalid filename", filename);
    return 0;
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
    hx_error(hx, "failed to read %s", filename);
    return 0;
  }
  
  if (hx->version == HX_VERSION_INVALID) {
    hx_error(hx, "invalid hx file version");
    return 0;
  }
  
  hx_stream_t stream = hx_stream_create(data, size, HX_STREAM_MODE_READ, hx_version_table[hx->version].endianness);
  hx->stream = stream;
  
  if (!hx_read(hx)) return 0;
  
  free(data);
  
}

void hx_context_write(hx_t *hx, const char* filename) {
  hx_stream_t ps = hx->stream;
  hx->stream = hx_stream_alloc(0x4FFFFF, HX_STREAM_MODE_WRITE, ps.endianness);
  memset(hx->stream.buf, 0, hx->stream.size);
  hx_write(hx);

  size_t size = hx->stream.size;
  hx->write_cb(filename, hx->stream.buf, 0, &size, hx->userdata);
  hx_stream_dealloc(&hx->stream);
  hx->stream = ps;
}

void hx_context_free(hx_t **hx) {
  free(*hx);
}
