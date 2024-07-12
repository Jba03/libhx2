/*****************************************************************
 # hxtool.c
 *****************************************************************
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#include "hx2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE *hstfile;

static char* read_callback(const char* filename, size_t pos, size_t *size) {
  
  FILE* fp = NULL;
  if (strcmp(filename, "RAYMAN3.HST") == 0 || strcmp(filename, "Data.hst") == 0) {
    if (!hstfile) hstfile = fopen(filename, "rb");
    fp = hstfile;
  } else {
    fp = fopen(filename, "rb");
  }
  
  if (!fp) return NULL;
  
  fseek(fp, 0, SEEK_END);
  size_t real_size = ftell(fp);
  if (*size > real_size) *size = real_size;
  fseek(fp, pos, SEEK_SET);
  
  char* data = malloc(*size);
  fread(data, *size, 1, fp);
  
  if (fp != hstfile) fclose(fp);
  return data;
}

static void write_callback(const char* filename, void* data, size_t pos, size_t *size) {
  FILE* fp = fopen(filename, "wb");
  fseek(fp, pos, SEEK_SET);
  fwrite(data, *size, 1, fp);
  fclose(fp);
}

int main(int argc, char** argv) {
  hx_t* hx = hx_context_alloc(0);
  hx_context_callback(hx, &read_callback, &write_callback);

  if (!hx_context_open(hx, argv[1])) {
    return -1;
  }
  
  int num_entries = 0;
  hx_entry_t* entries;
  hx_context_get_entries(hx, &entries, &num_entries);
  
  for (int i = 0; i < num_entries; i++) {
    hx_entry_t *entry = entries + i;
    if (entry->class == HX_CLASS_WAVE_FILE_ID_OBJECT) {
      struct hx_wave_file_id_object* obj = entry->data;
      struct hx_audio_stream* audio = obj->audio_stream;
      
      char name[256]; if (obj->id_obj.flags & (1 << 0)) sprintf(name, "Output/EXT-%016llX.wav", entry->cuuid);
      else sprintf(name, "Output/%016llX.wav", entry->cuuid);
      
      if (audio->codec == HX_CODEC_NGC_DSP) {
        hx_audio_stream_t out;
        ngc_dsp_decode(hx, audio, &out);
        hx_audio_stream_write_wav(hx, &out, name);
      }
    }
  }
  
  //hx_context_write(hx, "output.hxg");
  hx_context_free(&hx);
}
