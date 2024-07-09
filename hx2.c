/*****************************************************************
 # libhx2: library for reading and writing ubi hxaudio files
 *****************************************************************
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#include "hx2.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#if defined(WIN32)
# define HX_EXPORT __declspec(dllexport) __attribute__((used))
#else
# define HX_EXPORT __attribute__((used))
#endif

#define HX_BIG_ENDIAN 1
#define HX_LITTLE_ENDIAN 0
#define HX_NATIVE_ENDIAN (!*(uint8_t*)&(uint16_t){1})

#define bswap16(data) ((uint16_t)((uint16_t)(data) << 8 | (uint16_t)(data) >> 8))
#define bswap32(data) ((uint32_t)(bswap16((uint32_t)(data)) << 16 | bswap16((uint32_t)(data) >> 16)))

#pragma mark - Stream

typedef struct hx_stream {
  uint8_t* buf;
  size_t size;
  size_t pos;
  uint8_t endianness;
} hx_stream_t;

hx_stream_t hx_stream_create(void *buf, size_t sz, uint8_t endianness) {
  hx_stream_t stream;
  stream.buf = buf;
  stream.size = sz;
  stream.pos = 0;
  stream.endianness = endianness;
  return stream;
}

hx_stream_t hx_stream_alloc(size_t sz, uint8_t endianness) {
  return hx_stream_create(malloc(sz), sz, endianness);
}

void hx_stream_free(hx_stream_t *s) {
  free(s->buf);
}

static int hx_stream_bswap(hx_stream_t *s) {
  return (s->endianness == HX_BIG_ENDIAN);
}

static void hx_stream_seek(hx_stream_t *s, size_t pos) {
  s->pos = pos;
}

static void hx_stream_advance(hx_stream_t *s, int offset) {
  s->pos += offset;
}

static void hx_stream_read8(hx_stream_t *s, uint8_t *data) {
  *data = *(uint8_t*)(s->buf + s->pos);
  s->pos += 1;
}

static void hx_stream_read16(hx_stream_t *s, uint16_t *data) {
  *data = *(uint16_t*)(s->buf + s->pos);
  *data = hx_stream_bswap(s) ? bswap16(*data) : *data;
  s->pos += 2;
}

static void hx_stream_read32(hx_stream_t *s, uint32_t *data) {
  *data = *(uint32_t*)(s->buf + s->pos);
  *data = hx_stream_bswap(s) ? bswap32(*data) : *data;
  s->pos += 4;
}

static void hx_stream_readfloat(hx_stream_t *s, float *data) {
  hx_stream_read32(s, (uint32_t*)data);
}

static void hx_stream_readstring(hx_stream_t *s, char** data, size_t length) {
  *data = malloc(length + 1);
  memset(*data, 0, length + 1);
  memcpy(*data, s->buf + s->pos, length);
  s->pos += length;
}

static void hx_stream_readcuuid(hx_stream_t *s, uint64_t *cuuid) {
  hx_stream_read32(s, (uint32_t*)cuuid + 1);
  hx_stream_read32(s, (uint32_t*)cuuid + 0);
}

static void hx_stream_write(hx_stream_t *s, void* data, size_t sz) {
  memcpy(s->buf + s->pos, data, sz);
  s->pos += sz;
}

static void hx_stream_write8(hx_stream_t *s, uint8_t *value) {
  uint8_t data = *value;
  *(uint8_t*)(s->buf + s->pos) = data;
  s->pos++;
}

static void hx_stream_write16(hx_stream_t *s, uint16_t *value) {
  uint16_t data = *value;
  if (hx_stream_bswap(s)) data = bswap16(data);
  *(uint16_t*)(s->buf + s->pos) = data;
  s->pos += 2;
}

static void hx_stream_write32(hx_stream_t *s, uint32_t *value) {
  uint32_t data = *value;
  if (hx_stream_bswap(s)) data = bswap32(data);
  *(uint32_t*)(s->buf + s->pos) = data;
  s->pos += 4;
}

static void hx_stream_writefloat(hx_stream_t *s, float *value) {
  hx_stream_write32(s, (uint32_t*)value);
}

static void hx_stream_writecuuid(hx_stream_t *s, uint64_t *cuuid) {
  hx_stream_write32(s, (uint32_t*)cuuid + 1);
  hx_stream_write32(s, (uint32_t*)cuuid + 0);
}

#pragma mark - HX

struct hx {
  int options;
  enum hx_version version;
  
  unsigned int num_entries;
  hx_entry_t* entries;
  
  hx_stream_t stream;
  hx_read_callback_t read_cb;
  hx_write_callback_t write_cb;
  char error[256];
};

struct hx_version_table_entry {
  const char* name;
  const char* platform;
  uint8_t endianness;
};

struct hx_class_table_entry {
  const char* name;
  int crossversion;
  int (*read)(hx_t*, hx_entry_t*);
  int (*write)(hx_t*, hx_entry_t*);
  void (*dealloc)(hx_t*, hx_entry_t*);
};

static const struct hx_class_table_entry hx_class_table[];

static const struct hx_version_table_entry hx_version_table[] = {
  [HX_VERSION_HXD] = {"hxd", "PC", HX_BIG_ENDIAN},
  [HX_VERSION_HXC] = {"hxc", "PC", HX_LITTLE_ENDIAN},
  [HX_VERSION_HX2] = {"hx2", "PS2", HX_LITTLE_ENDIAN},
  [HX_VERSION_HXG] = {"hxg", "GC", HX_BIG_ENDIAN},
  [HX_VERSION_HXX] = {"hxx", "XBox", HX_BIG_ENDIAN},
  [HX_VERSION_HX3] = {"hx3", "PS3", HX_LITTLE_ENDIAN},
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
  if (!strncmp(name, "RandomResData", 13)) return HX_CLASS_RANDOM_RESOURCE_DATA;
  if (!strncmp(name, "ProgramResData", 14)) return HX_CLASS_PROGRAM_RESOURCE_DATA;
  if (!strncmp(name, "WaveFileIdObj", 13)) return HX_CLASS_WAVE_FILE_ID_OBJECT;
  return HX_CLASS_INVALID;
}

static void hx_class_to_string(hx_t *hx, enum hx_class class, char *out, uint32_t *out_sz) {
  const struct hx_version_table_entry v = hx_version_table[hx->version];
  const struct hx_class_table_entry c = hx_class_table[class];
  *out_sz += sprintf(out, "C%s%s", c.crossversion ? "" : v.platform,  c.name);
}

void hx_context_get_entries(hx_t *hx, hx_entry_t** entries, int *count) {
  *entries = hx->entries;
  *count = hx->num_entries;
}

hx_entry_t *hx_context_entry_lookup(hx_t *hx, uint64_t cuuid) {
  for (unsigned int i = 0; i < hx->num_entries; i++)
    if (hx->entries[i].cuuid == cuuid) return &hx->entries[i];
  return NULL;
}

static int hx_error(hx_t *hx, const char* format, ...) {
  va_list args;
  va_start(args, format);
  vsprintf(hx->error, format, args);
  va_end(args);
  return 0;
}

#pragma mark - BNH

//HX_EXPORT void hx_bnh_read(hx_t* hx, char* buf) {
//  unsigned int id1, id2;
//  char fix[8], level[8], transit[8], name[256];
//  while (sscanf(buf, "cuuid( 0x%X, 0x%X ) * fixe %s * level %s * transition %s * file %s", &id1, &id2, fix, level, transit, name) == 6) {
//    while (*buf != '\n') buf++; buf++;
//    printf("cuuid( 0x%08X, 0x%08X ) * fixe %s * level %s * transition %s * name %s\n", id1, id2, fix, level, transit, name);
//  }
//}


#pragma mark - Wave writer

static int hx_waveformat_header(hx_stream_t *s, struct hx_waveformat_header *h, int write) {
  (write ? hx_stream_write32 : hx_stream_read32)(s, &h->riff_code);
  (write ? hx_stream_write32 : hx_stream_read32)(s, &h->riff_length);
  (write ? hx_stream_write32 : hx_stream_read32)(s, &h->wave_code);
  (write ? hx_stream_write32 : hx_stream_read32)(s, &h->fmt_code);
  (write ? hx_stream_write32 : hx_stream_read32)(s, &h->chunk_size);
  (write ? hx_stream_write16 : hx_stream_read16)(s, &h->format);
  (write ? hx_stream_write16 : hx_stream_read16)(s, &h->channels);
  (write ? hx_stream_write32 : hx_stream_read32)(s, &h->sample_rate);
  (write ? hx_stream_write32 : hx_stream_read32)(s, &h->bytes_per_second);
  (write ? hx_stream_write16 : hx_stream_read16)(s, &h->alignment);
  (write ? hx_stream_write16 : hx_stream_read16)(s, &h->bits_per_sample);
  (write ? hx_stream_write32 : hx_stream_read32)(s, &h->data_code);
  (write ? hx_stream_write32 : hx_stream_read32)(s, &h->data_length);
  return (h->riff_code == 0x46464952) && (h->wave_code == 0x45564157) && (h->fmt_code == 0x20746D66);
}

static void hx_waveformat_write(hx_t *hx, const char* filename, struct hx_waveformat_header* header, void* data) {
  header->riff_length = header->data_length + sizeof(*header) - 8;
  size_t size = sizeof(struct hx_waveformat_header) + header->data_length;
  hx_stream_t stream = hx_stream_alloc(size, HX_NATIVE_ENDIAN);
  hx_waveformat_header(&stream, header, 1);
  hx_stream_write(&stream, data, header->data_length);
  hx->write_cb(filename, stream.buf, 0, &size);
  hx_stream_free(&stream);
}

static void hx_waveformat_default_header(struct hx_waveformat_header *h) {
  h->riff_code = *(uint32_t*)"RIFF";
  h->riff_length = 0;
  h->wave_code = *(uint32_t*)"WAVE";
  h->fmt_code = *(uint32_t*)"fmt ";
  h->chunk_size = 16;
  h->format = HX_CODEC_PCM;
  h->channels = 1;
  h->sample_rate = 0;
  h->bytes_per_second = 0;
  h->alignment = 16;
  h->bits_per_sample = 16;
  h->data_code = *(uint32_t*)"data";
  h->data_length = 0;
}

int hx_audio_stream_write_wav(hx_t *hx, hx_audio_stream_t *s, const char* filename) {
  if (s->codec != HX_CODEC_PCM) {
    hx_error(hx, "wave file data must be pcm encoded\n");
  }
  
  struct hx_waveformat_header header;
  hx_waveformat_default_header(&header);
  header.sample_rate = s->sample_rate;
  header.channels = s->num_channels;
  header.bits_per_sample = 16;
  header.bytes_per_second = s->num_channels * s->sample_rate * header.bits_per_sample / 8;
  header.alignment = header.channels * header.bits_per_sample / 8;
  header.data_length = s->size;

  hx_waveformat_write(hx, filename, &header, s->data);
}

#pragma mark - Class: EventResData

static int hx_class_read_event_resource_data(hx_t *hx, hx_entry_t *entry) {
  hx_stream_t *s = &hx->stream;
  
  struct hx_event_resource_data *data = malloc(sizeof *data);
  hx_stream_read32(s, &data->type);
  
  uint32_t name_length;
  hx_stream_read32(s, &name_length);
  hx_stream_readstring(s, &data->name, name_length);
  hx_stream_read32(s, &data->flags);
  hx_stream_readcuuid(s, &data->link_cuuid);
  hx_stream_readfloat(s, data->f_param + 0);
  hx_stream_readfloat(s, data->f_param + 1);
  hx_stream_readfloat(s, data->f_param + 2);
  hx_stream_readfloat(s, data->f_param + 3);
  
  printf("EventResData @ 0x%zX (%d, %s, link1 = %016llX)\n", s->pos, data->type, data->name, data->link_cuuid);
  entry->data = data;
  
  return 1;
}

static int hx_class_write_event_resource_data(hx_t *hx, hx_entry_t *entry) {
  hx_stream_t *s = &hx->stream;
  hx_event_resource_data_t *data = entry->data;
  uint32_t name_length = (uint32_t)strlen(data->name);
  hx_stream_write32(s, &data->type);
  hx_stream_write32(s, &name_length);
  hx_stream_write(s, data->name, name_length);
  hx_stream_write32(s, &data->flags);
  hx_stream_writecuuid(s, &data->link_cuuid);
  hx_stream_writefloat(s, data->f_param + 0);
  hx_stream_writefloat(s, data->f_param + 1);
  hx_stream_writefloat(s, data->f_param + 2);
  hx_stream_writefloat(s, data->f_param + 3);
  return 1;
}

#pragma mark - SuperClass: WavResObj

static void hx_class_read_wav_res_obj(hx_t *hx, hx_wav_resource_object_t *data) {
  hx_stream_t *s = &hx->stream;
  hx_stream_read32(s, &data->id);
  
  if (hx->version == HX_VERSION_HXC) {
    uint32_t name_length;
    hx_stream_read32(s, &name_length);
    hx_stream_readstring(s, &data->name, name_length);
  }
  
  if (hx->version == HX_VERSION_HXG) {
    hx_stream_read32(s, &data->size);
  }
  
  hx_stream_readfloat(s, &data->c0);
  hx_stream_readfloat(s, &data->c1);
  hx_stream_readfloat(s, &data->c2);
  hx_stream_read8(s, &data->flags);
}

static void hx_class_write_wav_res_obj(hx_t *hx, hx_wav_resource_object_t *data) {
  hx_stream_t *s = &hx->stream;
  hx_stream_write32(s, &data->id);
  
  if (hx->version == HX_VERSION_HXC) {
    uint32_t name_length = (uint32_t)strlen(data->name);
    hx_stream_write32(s, &name_length);
    hx_stream_write(s, data->name, name_length);
  }
  
  if (hx->version == HX_VERSION_HXG) {
    hx_stream_write32(s, &data->size);
  }
  
  hx_stream_writefloat(s, &data->c0);
  hx_stream_writefloat(s, &data->c1);
  hx_stream_writefloat(s, &data->c2);
  hx_stream_write8(s, &data->flags);
}

#pragma mark - Class: WavResData

#define HX_WAVRESDATA_FLAG_MULTIPLE (1 << 1)

static int hx_class_read_wave_resource_data(hx_t *hx, hx_entry_t *entry) {
  struct hx_wav_resource_data *data = malloc(sizeof *data);
  /* read parent class (WavResObj) */
  hx_class_read_wav_res_obj(hx, &data->res_data);
  /* number of links are 0 by default */
  data->num_links = 0;
  
  hx_stream_t *s = &hx->stream;
  hx_stream_readcuuid(s, &data->default_cuuid);
  
  // 1C = 1
  // 18 = 1
  // TODO: Figure out the rest of the flags
  if (data->res_data.flags & HX_WAVRESDATA_FLAG_MULTIPLE) {
    printf("offset: 0x%zX\n", s->pos);
    /* default cuuid should be zero on GC */
    if (hx->version == HX_VERSION_HXG) assert(data->default_cuuid == 0);
    hx_stream_read32(s, &data->num_links);
    data->links = malloc(sizeof(struct hx_wav_resource_data_link) * data->num_links);
  }
  
  printf("WavResData @ 0x%zX (flags = %d, sz = %d, flags = %X, default = %016llX numLinks = %d)\n", s->pos, data->res_data.flags, data->res_data.size, data->res_data.flags, data->default_cuuid, data->num_links);
  
  for (int i = 0; i < data->num_links; i++) {
    hx_stream_read32(s, (uint32_t*)data->links[i].language_code);
    hx_stream_readcuuid(s, &data->links[i].cuuid);
    *(uint32_t*)data->links[i].language_code = bswap32(*(uint32_t*)data->links[i].language_code);
    printf("\tlink[%d]: (language = %.2s, cuuid = %016llX)\n", i, data->links[i].language_code, data->links[i].cuuid);
  }
  
  entry->data = data;
  return 1;
}

static int hx_class_write_wave_resource_data(hx_t *hx, hx_entry_t *entry) {
  hx_wav_resource_data_t *data = entry->data;
  hx_class_write_wav_res_obj(hx, &data->res_data);
  hx_stream_writecuuid(&hx->stream, &data->default_cuuid);
  
  if (data->res_data.flags & HX_WAVRESDATA_FLAG_MULTIPLE) {
    /* default cuuid should be zero on GC */
    if (hx->version == HX_VERSION_HXG) assert(data->default_cuuid == 0);
    hx_stream_write32(&hx->stream, &data->num_links);
  }
  
  for (int i = 0; i < data->num_links; i++) {
    uint32_t language_code = bswap32(*(uint32_t*)data->links[i].language_code);
    hx_stream_write32(&hx->stream, (uint32_t*)&language_code);
    hx_stream_writecuuid(&hx->stream, &data->links[i].cuuid);
  }
}

#pragma mark - Class: RandomResData

static int hx_class_read_random_resource_data(hx_t *hx, hx_entry_t *entry) {
  hx_stream_t *s = &hx->stream;
  
  struct hx_random_resource_data *data = malloc(sizeof *data);
  hx_stream_read32(s, &data->flags);
  hx_stream_readfloat(s, &data->offset);
  hx_stream_readfloat(s, &data->throw_probability);
  hx_stream_read32(s, &data->num_links);
  
  printf("RandomResData @ 0x%zX (flags = %d, off = %f, throwProbability = %f, numLinks = %d)\n", s->pos, data->flags, data->offset, data->throw_probability, data->num_links);
  
  data->links = malloc(sizeof(struct hx_random_resource_data_link) * data->num_links);
  for (unsigned i = 0; i < data->num_links; i++) {
    hx_stream_readfloat(s, &data->links[i].probability);
    hx_stream_readcuuid(s, &data->links[i].cuuid);
    printf("\tlink[%d]: (probability = %f, cuuid = %016llX)\n", i, data->links[i].probability, data->links[i].cuuid);
  }
  
  entry->data = data;
  return 1;
}

static int hx_class_write_random_resource_data(hx_t *hx, hx_entry_t *entry) {
  hx_stream_t *s = &hx->stream;
  hx_random_resource_data_t *data = entry->data;
  hx_stream_write32(s, &data->flags);
  hx_stream_writefloat(s, &data->offset);
  hx_stream_writefloat(s, &data->throw_probability);
  hx_stream_write32(s, &data->num_links);
  for (int i = 0; i < data->num_links; i++) {
    hx_stream_writefloat(s, &data->links[i].probability);
    hx_stream_writecuuid(s, &data->links[i].cuuid);
  }
  return 1;
}

#pragma mark - Class: ProgramResData

struct hx_class_program_res_data {
//  struct hx_class_res_data res_data;
//  uint32_t num_linked_cuuids;
//  uint64_t default_cuuid;
  
  uint32_t type;
  uint32_t unk1;
  uint32_t unk2;
  uint32_t unk3;
  uint32_t unk4;
  uint32_t unk5_zero;
  uint32_t unk6_zero;
  uint32_t unk7;
  uint32_t unk8;
  uint32_t unk9;
  uint32_t program_length;
  
  
  uint32_t num_trailer_links;
  uint64_t trailer_links[256];
};

static int hx_class_read_program_resource_data(hx_t *hx, hx_entry_t *entry) {
  struct hx_class_program_res_data *data = malloc(sizeof *data);
  /* read parent class (CResData) */
 // hx_class_read_resource_data(s, &data->res_data);
  hx_stream_t *s = &hx->stream;
  
 
  
  char name[256];
  uint32_t length = 0;
  hx_class_to_string(hx, entry->class, name, &length);
  
  /* just copy the entire internal entry (minus the header) */
  entry->tmp_file_size = entry->file_size - (4 + length + 8);
  entry->data = malloc(entry->file_size);
  memcpy(entry->data, s->buf + s->pos, entry->file_size);
  
  //uint32_t type;
//  hx_stream_read32(s, &data->type);
//  hx_stream_read32(s, &data->unk1);
//  hx_stream_read32(s, &data->unk2);
//  hx_stream_read32(s, &data->unk3);
//  hx_stream_read32(s, &data->unk4);
//  hx_stream_read32(s, &data->unk5_zero);
//  hx_stream_read32(s, &data->unk6_zero);
//  hx_stream_read32(s, &data->unk7);
//  hx_stream_read32(s, &data->unk8);
//  hx_stream_read32(s, &data->unk9);
//  hx_stream_read32(s, &data->program_length);
  //hx_stream_read32(s, &data->unk1);
  
  printf("ProgramResData @ 0x%zX (type = %X, programLength = %X, unk3 = %d, unk4 = %d)\n", s->pos, data->type, data->program_length, data->unk3, data->unk4);
  
  
//  uint32_t type;
//  hx_stream_read32(s, &type);
  
  /* Trailer */
//  hx_stream_read32(s, &data->num_trailer_links);
//  for (int i = 0; i < data->num_trailer_links; i++) {
//    hx_stream_readcuuid(s, data->trailer_links + i);
//  }
  
  printf("a\n");
  
 
}

static int hx_class_write_program_resource_data(hx_t *hx, hx_entry_t *entry) {
  hx_stream_write(&hx->stream, entry->data, entry->tmp_file_size);
}

#pragma mark - SuperClass: IdObjPtr

#define HX_ID_OBJECT_POINTER_FLAG_EXTERNAL  (1 << 0)

static void hx_class_read_id_object_pointer(hx_t *hx, hx_id_object_pointer_t *data) {
  hx_stream_t *s = &hx->stream;
  hx_stream_read32(s, &data->id);
  hx_stream_readfloat(s, &data->unknown);
  if (hx->version == HX_VERSION_HXG) {
    hx_stream_read32(s, &data->flags);
    hx_stream_read32(s, &data->unknown2);
  } else {
    uint8_t tmp_flags;
    hx_stream_read8(s, &tmp_flags);
    data->flags = tmp_flags;
  }
}

static void hx_class_write_id_object_pointer(hx_t *hx, hx_id_object_pointer_t *data) {
  hx_stream_t *s = &hx->stream;
  hx_stream_write32(s, &data->id);
  hx_stream_writefloat(s, &data->unknown);
  if (hx->version == HX_VERSION_HXG) {
    hx_stream_write32(s, &data->flags);
    hx_stream_write32(s, &data->unknown2);
  } else {
    uint8_t flags = data->flags;
    hx_stream_write8(s, &flags);
  }
}

#pragma mark - Class: WaveFileIdObj

/* Read WaveFileIdObj data from an external file */
static int hx_read_wave_file_id_object_ext(hx_t *hx, hx_wave_file_id_object_t *data, hx_audio_stream_t *audio_stream) {
  size_t sz = data->ext_stream_size;
  audio_stream->data = (int16_t*)hx->read_cb(data->ext_stream_filename, data->ext_stream_offset, &sz);
}

/* Read a WaveFileIdObj entry */
static int hx_class_read_wave_file_id_obj(hx_t *hx, hx_entry_t *entry) {
  hx_stream_t *s = &hx->stream;
  hx_wave_file_id_object_t *data = malloc(sizeof(*data));
  hx_class_read_id_object_pointer(hx, &data->id_obj);
  
  printf("WaveFileIdObj @ 0x%zX (id = %d, unk = %f, mode = %X)\n", s->pos, data->id_obj.id, data->id_obj.unknown, data->id_obj.flags);
  
  if (data->id_obj.flags & HX_ID_OBJECT_POINTER_FLAG_EXTERNAL) {
    uint32_t name_length;
    hx_stream_read32(s, &name_length);
    hx_stream_readstring(s, &data->ext_stream_filename, name_length);
  }
  
  if (!hx_waveformat_header(&hx->stream, &data->wave_header, 0)) {
    hx_error(hx, "failed to read wave format header");
    printf("error: %s\n", hx_error_string(hx));
    return 0;
  }
  
  hx_audio_stream_t *audio_stream = malloc(sizeof(*audio_stream));
  audio_stream->codec = data->wave_header.format;
  audio_stream->num_channels = data->wave_header.channels;
  audio_stream->endianness = s->endianness;
  audio_stream->size = data->wave_header.data_length;
  audio_stream->sample_rate = data->wave_header.sample_rate;
  //audio_stream->num_samples =
  
  data->audio_stream = audio_stream;
  
  printf("codec: 0x%X\n", data->wave_header.format);
  
  if (data->id_obj.flags & HX_ID_OBJECT_POINTER_FLAG_EXTERNAL) {
    /* data code must be "datx" */
    assert(data->wave_header.data_code == 0x78746164);
    /* the internal data length must be 8 */
    assert(data->wave_header.data_length == 8);
    
    hx_stream_read32(s, &data->ext_stream_size);
    hx_stream_read32(s, &data->ext_stream_offset);
    
    size_t sz = data->ext_stream_size;
    if (!(audio_stream->data = (int16_t*)hx->read_cb(data->ext_stream_filename, data->ext_stream_offset, &sz))) {
      hx_error(hx, "failed to read external audio stream data");
      return 0;
    }
  } else {
    /* data code must be "data" */
    assert(data->wave_header.data_code == 0x61746164);
    /* read internal stream data */
    audio_stream->data = malloc(data->wave_header.data_length);
    memcpy(audio_stream->data, s->buf + s->pos, data->wave_header.data_length);
    
    // 278D0
    // 80490
    // 80A90
    // 563560A36CFE3EDF
    hx_stream_advance(s, data->wave_header.data_length);
  }
  
  /* Temporary solution: determine the length of the
   * rest of the file and copy the data to a buffer.
   * TODO: Read more wave format chunk types */
  data->extra_wave_data_length = (data->wave_header.riff_length + 8) - data->wave_header.data_length - sizeof(struct hx_waveformat_header);
  data->extra_wave_data = NULL;
  if (data->id_obj.flags & HX_ID_OBJECT_POINTER_FLAG_EXTERNAL)
    data->extra_wave_data_length += 4;
  if (data->extra_wave_data_length > 0) {
    if (!(data->id_obj.flags & HX_ID_OBJECT_POINTER_FLAG_EXTERNAL)) data->extra_wave_data_length += 1;
    
    data->extra_wave_data = malloc(data->extra_wave_data_length);
    memcpy(data->extra_wave_data, s->buf + s->pos, data->extra_wave_data_length);
    hx_stream_advance(s, data->extra_wave_data_length);
  }
  
  entry->data = data;
  return 1;
}

static int hx_class_write_wave_file_id_obj(hx_t *hx, hx_entry_t *entry) {
  hx_stream_t *s = &hx->stream;
  hx_wave_file_id_object_t *data = entry->data;
  hx_class_write_id_object_pointer(hx, &data->id_obj);
  
  if (data->id_obj.flags & HX_ID_OBJECT_POINTER_FLAG_EXTERNAL) {
    uint32_t name_length = (uint32_t)strlen(data->ext_stream_filename);
    hx_stream_write32(s, &name_length);
    hx_stream_write(s, data->ext_stream_filename, name_length);
  }
  
  hx_audio_stream_t *audio_stream = data->audio_stream;
  data->wave_header.format = audio_stream->codec;
  data->wave_header.channels = audio_stream->num_channels;
  data->wave_header.data_length = audio_stream->size;
  data->wave_header.sample_rate = audio_stream->sample_rate;
  data->wave_header.data_length = audio_stream->size;
  
  if (!hx_waveformat_header(&hx->stream, &data->wave_header, 1)) {
    hx_error(hx, "failed to write wave format header");
    return 0;
  }
  
  if (data->id_obj.flags & HX_ID_OBJECT_POINTER_FLAG_EXTERNAL) {
    data->wave_header.data_code = 0x78746164; /* "datx" */
    data->wave_header.data_length = 8;
    hx_stream_write32(s, &data->ext_stream_size);
    hx_stream_write32(s, &data->ext_stream_offset);
  } else {
    data->wave_header.data_code = 0x61746164; /* "data" */
    hx_stream_write(s, audio_stream->data, data->wave_header.data_length);
    /* todo... */
  }
  
  if (data->extra_wave_data) {
    hx_stream_write(s, data->extra_wave_data, data->extra_wave_data_length);
  }
  
  entry->data = data;
  return 1;
}

static const struct hx_class_table_entry hx_class_table[] = {
  [HX_CLASS_EVENT_RESOURCE_DATA] = {"EventResData", 1, hx_class_read_event_resource_data, hx_class_write_event_resource_data},
  [HX_CLASS_WAVE_RESOURCE_DATA] = {"WavResData", 0, hx_class_read_wave_resource_data, hx_class_write_wave_resource_data},
  [HX_CLASS_RANDOM_RESOURCE_DATA] = {"RandomResData", 1, hx_class_read_random_resource_data, hx_class_write_random_resource_data},
  [HX_CLASS_PROGRAM_RESOURCE_DATA] = {"ProgramResData", 1, hx_class_read_program_resource_data, hx_class_write_program_resource_data},
  [HX_CLASS_WAVE_FILE_ID_OBJECT] = {"WaveFileIdObj", 0, hx_class_read_wave_file_id_obj, hx_class_write_wave_file_id_obj},
};

static int hx_entry_read(hx_t *hx, hx_entry_t *entry) {
  hx_stream_t *s = &hx->stream;
  
  uint32_t classname_length;
  hx_stream_read32(s, &classname_length);
  enum hx_class hclass = hx_class_from_string((char*)s->buf + s->pos);
  
  if (hclass != entry->class) {
    fprintf(stderr, "[libhx] header class name does not match index class name (%X != %X)\n", entry->class, hclass);
    return -1;
  }
  
  hx_stream_advance(s, classname_length);
  
  uint64_t cuuid;
  hx_stream_readcuuid(s, &cuuid);
  if (cuuid != entry->cuuid) {
    fprintf(stderr, "[libhx] header cuuid does not match index cuuid (%016llX != %016llX)\n", entry->cuuid, cuuid);
    return -1;
  }
  
  if (entry->class != HX_CLASS_INVALID) {
    return hx_class_table[entry->class].read(hx, entry);
  }
  
  return -1;
}

static int hx_entry_write(hx_t *hx, hx_entry_t *entry) {
  hx_stream_t *s = &hx->stream;
  
  entry->file_offset = s->pos;
  
  char name[256];
  uint32_t name_length = 0;
  hx_class_to_string(hx, entry->class, name, &name_length);
  hx_stream_write32(s, &name_length);
  hx_stream_write(s, name, name_length);
  hx_stream_writecuuid(s, &entry->cuuid);
  
  if (entry->class != HX_CLASS_INVALID) {
    return hx_class_table[entry->class].write(hx, entry);
  }
}

static int hx_read(hx_t *hx) {
  hx_stream_t *s = &hx->stream;
  uint32_t index_table_offset;
  uint32_t index_code;
  uint32_t index_type;
  uint32_t num_entries;
  
  hx_stream_read32(s, &index_table_offset);
  hx_stream_seek(s, index_table_offset);
  hx_stream_read32(s, &index_code);
  hx_stream_read32(s, &index_type);
  hx_stream_read32(s, &num_entries);
  
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
    uint32_t classname_length;
    hx_stream_read32(s, &classname_length);
    
    char classname[classname_length + 1];
    memset(classname, 0, classname_length + 1);
    memcpy(classname, s->buf + s->pos, classname_length);
    hx_stream_advance(s, classname_length);
    
    hx_entry_t* entry = &hx->entries[hx->num_entries - num_entries - 1];
    entry->class = hx_class_from_string(classname);
    entry->num_links = 0;
    entry->num_languages = 0;
    entry->data = NULL;
    
    uint32_t zero;
    hx_stream_readcuuid(s, &entry->cuuid);
    hx_stream_read32(s, &entry->file_offset);
    hx_stream_read32(s, &entry->file_size);
    hx_stream_read32(s, &zero);
    hx_stream_read32(s, &entry->num_links);
    
    assert(zero == 0x00);
    
    if (index_type == 0x2) {
      entry->links = malloc(sizeof(*entry->links) * entry->num_links);
      for (int i = 0; i < entry->num_links; i++) {
        hx_stream_readcuuid(s, entry->links + i);
      }

      hx_stream_read32(s, &entry->num_languages);
      entry->language_links = malloc(sizeof(hx_entry_language_link_t) * entry->num_languages);
      
      for (int i = 0; i < entry->num_languages; i++) {
        hx_stream_read32(s, &entry->language_links[i].code);
        hx_stream_read32(s, &entry->language_links[i].unknown);
        hx_stream_readcuuid(s, &entry->language_links[i].cuuid);
      }
    }

    printf("[%d] %s (%016llX)\n", hx->num_entries - num_entries - 1, classname, entry->cuuid);
    
    size_t pos = s->pos;
    hx_stream_seek(s, entry->file_offset);
    hx_entry_read(hx, entry);
    hx_stream_seek(s, pos);
    
    printf("\n\n");
  }
  
  return 1;
}

static void hx_write(hx_t *hx) {
  hx_stream_t *s = &hx->stream;
  /* Allocate index stream */
  uint8_t *index_buffer = malloc(hx->num_entries * 0xFF);
  hx_stream_t index_stream = hx_stream_create(index_buffer, hx->num_entries * 0xFF, s->endianness);
  /* Reserve space for index offset */
  s->pos += 4;
  
  uint32_t index_code = 0x58444E49; /* "INDX" */
  uint32_t index_type = 2;
  uint32_t num_entries = hx->num_entries;
  
  hx_stream_write32(&index_stream, &index_code);
  hx_stream_write32(&index_stream, &index_type);
  hx_stream_write32(&index_stream, &num_entries);
  
  while (num_entries--) {
    hx_entry_t entry = hx->entries[hx->num_entries - num_entries - 1];
    
    char classname[256];
    uint32_t classname_length = 0;
    hx_class_to_string(hx, entry.class, classname, &classname_length);
    
    printf("Writing [%d]: %s (%016llX)\n", hx->num_entries - num_entries - 1, classname, entry.cuuid);
    hx_entry_write(hx, &entry);

    hx_stream_write32(&index_stream, &classname_length);
    hx_stream_write(&index_stream, classname, classname_length);

    uint32_t zero = 0;
    hx_stream_writecuuid(&index_stream, &entry.cuuid);
    hx_stream_write32(&index_stream, &entry.file_offset);
    hx_stream_write32(&index_stream, &entry.file_size);
    hx_stream_write32(&index_stream, &zero);
    hx_stream_write32(&index_stream, &entry.num_links);
    
    if (index_type == 0x2) {
      for (int i = 0; i < entry.num_links; i++) {
        hx_stream_writecuuid(&index_stream, entry.links + i);
      }

      hx_stream_write32(&index_stream, &entry.num_languages);
      for (int i = 0; i < entry.num_languages; i++) {
        hx_stream_write32(&index_stream, &entry.language_links[i].code);
        hx_stream_write32(&index_stream, &entry.language_links[i].unknown);
        hx_stream_writecuuid(&index_stream, &entry.language_links[i].cuuid);
      }
    }
  }
  
  /* Copy the index to the end of the file */
  uint32_t index_size = index_stream.pos;
  uint32_t index_offset = s->pos;
  hx_stream_write(s, index_stream.buf, index_size);
  
  s->size = s->pos + (8 * 4);
  memset(s->buf + s->pos, 0, 8 * 4);
  
  hx_stream_seek(s, 0);
  hx_stream_write32(s, &index_offset);
  
  free(index_buffer);

  return 0;
}

#pragma mark - HX

hx_t *hx_context_alloc(int options) {
  hx_t *hx = malloc(sizeof(*hx));
  hx->options = options;
  hx->version = HX_VERSION_INVALID;
  return hx;
}

void hx_context_callback(hx_t *hx, hx_read_callback_t read, hx_write_callback_t write) {
  hx->read_cb = read;
  hx->write_cb = write;
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
  uint8_t* data = hx->read_cb(filename, 0, &size);
  if (!data) {
    hx_error(hx, "failed to read %s", filename);
    return 0;
  }
  
  if (hx->version == HX_VERSION_INVALID) {
    hx_error(hx, "invalid hx file version");
    return 0;
  }
  
  hx_stream_t stream = hx_stream_create(data, size, hx_version_table[hx->version].endianness);
  hx->stream = stream;
  
  if (!hx_read(hx)) return 0;
  
  free(data);
  
}

void hx_context_write(hx_t *hx, const char* filename) {
  hx_stream_t ps = hx->stream;
  hx->stream = hx_stream_create(malloc(0x4FFFFF), 0x4FFFFF, ps.endianness);
  memset(hx->stream.buf, 0, hx->stream.size);
  hx_write(hx);
  
  size_t size = hx->stream.size;
  hx->write_cb(filename, hx->stream.buf, 0, &size);
  hx_stream_free(&hx->stream);
  hx->stream = ps;
}

void hx_context_free(hx_t **hx) {
  free(*hx);
}

const char* hx_error_string(hx_t *hx) {
  return hx->error;
}

#pragma mark - Codec

#define NGC_DSP_SAMPLES_PER_FRAME 14

static uint32_t ngc_dsp_pcm_size(uint32_t sample_count) {
  uint32_t frames = sample_count / NGC_DSP_SAMPLES_PER_FRAME;
  if (sample_count % NGC_DSP_SAMPLES_PER_FRAME) frames++;
  return frames * NGC_DSP_SAMPLES_PER_FRAME * sizeof(int16_t);
}

struct ngc_dsp_adpcm {
  uint32_t num_samples, num_nibbles;
  uint32_t sample_rate;
  uint16_t loop, format;
  uint32_t sa, ea, ca;
  uint16_t c[16], gain, ps;
  uint16_t yn1, yn2, lps, lyn1,lyn2;
  int32_t hst1, hst2;
  uint32_t remaining;
};

static void ngc_dsp_read_header(hx_stream_t *s, struct ngc_dsp_adpcm *adpcm) {
  hx_stream_read32(s, &adpcm->num_samples);
  hx_stream_read32(s, &adpcm->num_nibbles);
  hx_stream_read32(s, &adpcm->sample_rate);
  hx_stream_read16(s, &adpcm->loop);
  hx_stream_read16(s, &adpcm->format);
  hx_stream_read32(s, &adpcm->sa);
  hx_stream_read32(s, &adpcm->ea);
  hx_stream_read32(s, &adpcm->ca);
  for (int i = 0; i < 16; i++) hx_stream_read16(s, &adpcm->c[i]);
  hx_stream_read16(s, &adpcm->gain);
  hx_stream_read16(s, &adpcm->ps);
  hx_stream_read16(s, &adpcm->yn1);
  hx_stream_read16(s, &adpcm->yn2);
  hx_stream_read16(s, &adpcm->lps);
  hx_stream_read16(s, &adpcm->lyn1);
  hx_stream_read16(s, &adpcm->lyn2);
  for (int i = 0; i < 11; i++) hx_stream_advance(s, 2);
}

int hx_decode_ngc_dsp(hx_t *hx, hx_audio_stream_t* in, hx_audio_stream_t *out) {
  hx_stream_t stream = hx_stream_create(in->data, in->size, in->endianness);
  
  uint32_t total_samples = 0;
  
  struct ngc_dsp_adpcm channels[in->num_channels];
  for (int c = 0; c < in->num_channels; c++) {
    ngc_dsp_read_header(&stream, channels + c);
    total_samples += channels[c].num_samples;
    channels[c].hst1 = channels[c].yn1;
    channels[c].hst2 = channels[c].yn2;
    channels[c].remaining = channels[c].num_samples;
  }
  
  out->sample_rate = in->sample_rate;
  out->codec = HX_CODEC_PCM;
  out->num_channels = in->num_channels;
  out->num_samples = total_samples;
  out->size = ngc_dsp_pcm_size(total_samples);
  out->data = malloc(out->size);
  
  int num_frames = ((total_samples + NGC_DSP_SAMPLES_PER_FRAME - 1) / NGC_DSP_SAMPLES_PER_FRAME);
  uint8_t* src = stream.buf + stream.pos;
  int16_t* dst = out->data;
  
  for (int i = 0; i < num_frames; i++) {
    for (int c = 0; c < out->num_channels; c++) {
      struct ngc_dsp_adpcm *adpcm = channels + c;
      
      const int8_t ps = *src++;
      const int32_t predictor = (ps >> 4) & 0xF;
      const int32_t scale = 1 << (ps & 0xF);
      const int16_t c1 = adpcm->c[predictor * 2 + 0];
      const int16_t c2 = adpcm->c[predictor * 2 + 1];
        
      int32_t hst1 = adpcm->hst1;
      int32_t hst2 = adpcm->hst2;
      int32_t count = (adpcm->remaining > NGC_DSP_SAMPLES_PER_FRAME) ? NGC_DSP_SAMPLES_PER_FRAME : adpcm->remaining;
        
      for (int s = 0; s < count; s++) {
        int sample = (s % 2) == 0 ? ((*src >> 4) & 0xF) : (*src++ & 0xF);
        sample = sample >= 8 ? sample - 16 : sample;
        sample = (((scale * sample) << 11) + 1024 + (c1*hst1 + c2*hst2)) >> 11;
        if (sample<INT16_MIN) sample=INT16_MIN;
        if (sample>INT16_MAX) sample=INT16_MAX;
        hst2 = hst1;
        dst[s * out->num_channels + c] = hst1 = sample;
      }
        
      adpcm->hst1 = hst1;
      adpcm->hst2 = hst2;
      adpcm->remaining -= count;
    }
    
    dst += NGC_DSP_SAMPLES_PER_FRAME * out->num_channels;
  }
  
  return 1;
}

int hx_encode_ngc_dsp(hx_t *hx, hx_audio_stream_t *in, hx_audio_stream_t *out) {
  return 0;
}
