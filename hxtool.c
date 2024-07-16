/*****************************************************************
 # hxtool.c: hx audio stream extraction/replacement tool
 *****************************************************************
 * libhx2: library for reading and writing ubi hxaudio files
 * Copyright (c) 2024 Jba03 <jba03@jba03.xyz>
 *****************************************************************/

#include "hx2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#define RESET   "\x1b[0m"
#define BLACK   "\x1b[30m"
#define RED     "\x1b[31m"
#define GREEN   "\x1b[32m"
#define YELLOW  "\x1b[33m"
#define BLUE    "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN    "\x1b[36m"
#define WHITE   "\x1b[37m"
#define BOLD    "\x1b[1m"

struct userdata {
  const char* workdir;
};

FILE *hstfile;

static char* read_callback(const char* filename, size_t pos, size_t *size, void* userdata) {
  
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

static void write_callback(const char* filename, void* data, size_t pos, size_t *size, void* userdata) {
  FILE* fp = fopen(filename, "wb");
  fseek(fp, pos, SEEK_SET);
  fwrite(data, *size, 1, fp);
  fclose(fp);
}

static int extract_entry(hx_t *hx, hx_entry_t *entry) {
  if (entry->class == HX_CLASS_WAVE_FILE_ID_OBJECT) {
    struct hx_wave_file_id_object* obj = entry->data;
    struct hx_audio_stream* audio = obj->audio_stream;

    char name[2048];
    memset(name, 0, 2048);
    if (obj->id_obj.flags & (1 << 0)) sprintf(name, "Output/EXT-%016llX.wav", entry->cuuid);
    else sprintf(name, "Output/%016llX.wav", entry->cuuid);
    
    if (audio->codec == HX_CODEC_DSP) {
      hx_audio_stream_t out;
      dsp_decode(hx, audio, &out);
      if (hx_audio_stream_write_wav(hx, &out, name)) return 1;
    } else if (audio->codec == HX_CODEC_PCM) {
      
      if (hx_audio_stream_write_wav(hx, audio, name)) return 1;
    }
  }
  
  return 0;
}

static const char* inputfn;
static char extract_id[256];

static int opt_info = 0;
static int opt_list = 0;
static int opt_extract_all = 0;
static int opt_extract_one = 0;

static struct option options[] = {
  {"info", no_argument, 0, 'i'},
  {"list", no_argument, 0, 'l'},
  {"extract-all", no_argument, 0, 'E'},
  {"extract", required_argument, 0, 'e'},
  {0, 0, 0, 0},
};

static void print_usage() {
  printf("usage: hxtool [options] infile                                                  \n");
  printf("                                                                                \n");
  printf("--info                  Print information about the input file.                 \n");
  printf("--list                  List entry data.                                        \n");
  printf("--extract-all           Extract all audio streams from the input file.          \n");
  printf("--extract <cuuid>       Extract a single audio stream from the input file, or   \n");
  printf("                        multiple if entry references more than one audio stream.\n");
  printf("                                                                                \n");
  printf("<cuuid> is a 64-bit hexadecimal string.                                         \n");
  printf("                                                                                \n");
}

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return -1;
  }
  
  int wants_extract_all = 0;
  int wants_extract_single = 0;
  int num_infiles = 0;
  
  char in_filename[2048];
  memset(in_filename, 0, 2048);
  
  int option_idx = 0, c = 0;
  while ((c = getopt_long(argc, argv, "lEe:", options, &option_idx)) != -1) {
    switch (c) {
      case 'i':
        opt_info = 1;
        break;
      case 'l':
        opt_list = 1;
        break;
      case 'E':
        opt_extract_all = 1;
        break;
      case 'e':
        opt_extract_one = 1;
        strcpy(extract_id, optarg);
        break;
      default:
        return -1;
    }
  }
  
  if (optind >= argc) {
    print_usage();
    return -1;
  }
  
  inputfn = argv[optind];
  
  static struct userdata udata = {};
  
  /* Allocate the main context */
  hx_t* hx = hx_context_alloc();
  hx_context_callback(hx, &read_callback, &write_callback, &udata);
  
  if (!hx_context_open(hx, inputfn))
    return -1;
  
  int num_entries = 0;
  hx_entry_t* entries;
  hx_context_get_entries(hx, &entries, &num_entries);
  
  if (opt_info) {
    printf("Number of entries: %d\n", num_entries);
    goto end;
  }
  
  if (opt_list) {
    for (int i = 0; i < num_entries; i++) {
      hx_entry_t *entry = entries + i;
      
      char name[256];
      memset(name, 0, 256);
      hx_class_to_string(hx, entry->class, name, NULL);
      
      printf(BOLD "%s" RESET WHITE " %016llX (%d)" RESET "\n", name, entry->cuuid, i);
      
      switch (entry->class) {
        case HX_CLASS_EVENT_RESOURCE_DATA: {
          hx_event_resource_data_t *data = entry->data;
          printf("  CUUID = %016llX\n", entry->cuuid);
          printf("  Name  = %s\n", data->name);
          printf("  Link  = %016llX\n", data->link_cuuid);
          break;
        }
          
        case HX_CLASS_WAVE_RESOURCE_DATA: {
          hx_wav_resource_data_t *data = entry->data;
          break;
        }
          
        case HX_CLASS_WAVE_FILE_ID_OBJECT: {
          hx_wave_file_id_object_t *data = entry->data;
          printf(" External: %s\n", data->ext_stream_size ? data->ext_stream_filename : "no");
          printf(" Channels: %d\n", data->wave_header.num_channels);
          printf(" Sample rate: %.3fkHz\n", data->wave_header.sample_rate / 1000.0f);
          
          int sz = hx_audio_stream_size(data->audio_stream);
          float sec = ((float)sz / data->wave_header.bytes_per_second) * data->wave_header.num_channels;
          int min = (int)(sec / 60) % 60;
          printf(" Duration: " CYAN BOLD "%02d:%02d:%06.3f\n" RESET, 0, min, sec);
          printf(" Format: " "%s" RESET "\n", hx_codec_name(data->wave_header.format));
          break;
        }
          
        default:
          break;
      }
      
      printf("\n");
    }
    
    return 0;
  }
  
  if (opt_extract_one) {
    hx_cuuid_t cuuid;
    sscanf(extract_id, "%016llX\n", &cuuid);
    hx_entry_t *entry = hx_context_entry_lookup(hx, cuuid);
    if (!entry) {
      fprintf(stderr, "Found no entry with CUUID %s\n", extract_id);
      return -1;
    }
    
    printf("Extracting audio stream from entry %s...\n", extract_id);
    if (!extract_entry(hx, entry)) return -1;
    printf("Done.\n");
  }
  
  if (opt_extract_all) {
    printf("Extracting audio streams from %s...\n", inputfn);
    int written = 0;
    for (int i = 0; i < num_entries; i++) {
      hx_entry_t *entry = entries + i;
      if (extract_entry(hx, entry)) written++;
    }
    printf("Done - wrote %d entries.\n", written);
  }
  
  //hx_context_write(hx, "output.hxg");
  
end:
  hx_context_free(&hx);
  return 0;
}
