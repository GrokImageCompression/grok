/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 */

/*
 * gpu_plugin_shared.h — Canonical shared types for the GPU codec plugin.
 *
 * This header defines the minimal structs, enums, and constants that form
 * the contract between a host JPEG 2000 codec and the GPU plugin.
 * Both projects include this single file.  The plugin is codec-agnostic.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <climits>

/* ── Platform export macros ──────────────────────────────────── */

#ifdef _WIN32
#define GPUP_CALLCONV __stdcall
#ifdef GPUP_STATIC
#define GPUP_API
#else
#ifdef GPUP_EXPORTS
#define GPUP_API __declspec(dllexport)
#else
#define GPUP_API __declspec(dllimport)
#endif
#endif
#else
#define GPUP_CALLCONV
#ifdef GPUP_STATIC
#define GPUP_API __attribute__((visibility("hidden")))
#else
#define GPUP_API __attribute__((visibility("default")))
#endif
#endif

/* ── Constants ───────────────────────────────────────────────── */

#define GPUP_PATH_LEN 4096
#define GPUP_MAX_LAYERS 256
#define GPUP_MAX_DECOMP_LVLS 32
#define GPUP_MAXRLVLS (GPUP_MAX_DECOMP_LVLS + 1)
#define GPUP_MAXBANDS (3 * GPUP_MAXRLVLS - 2)
#define GPUP_MAX_SUPPORTED_PREC 16
#define GPUP_BIBO_EXTRA_BITS 7
#define GPUP_MAX_PASSES (3 * (GPUP_MAX_SUPPORTED_PREC + GPUP_BIBO_EXTRA_BITS) - 2)
#define GPUP_NUM_COMMENTS 256
#define GPUP_BUFFER_ALIGNMENT 64

/* ── Logging callback ────────────────────────────────────────── */

typedef void (*gpup_msg_callback)(const char* msg, void* client_data);

/* ── Opaque codec handle ─────────────────────────────────────── */

typedef void gpup_codec;

/* ── Enums ───────────────────────────────────────────────────── */

typedef enum
{
  GPUP_PROG_UNKNOWN = -1,
  GPUP_LRCP = 0,
  GPUP_RLCP = 1,
  GPUP_RPCL = 2,
  GPUP_PCRL = 3,
  GPUP_CPRL = 4,
  GPUP_NUM_PROGRESSION_ORDERS = 5
} gpup_prog_order;

typedef enum
{
  GPUP_CLRSPC_UNKNOWN = 0,
  GPUP_CLRSPC_SRGB = 2,
  GPUP_CLRSPC_GRAY = 3,
  GPUP_CLRSPC_SYCC = 4,
  GPUP_CLRSPC_EYCC = 5,
  GPUP_CLRSPC_CMYK = 6,
  GPUP_CLRSPC_DEFAULT_CIE = 7,
  GPUP_CLRSPC_CUSTOM_CIE = 8,
  GPUP_CLRSPC_ICC = 9
} gpup_color_space;

typedef enum
{
  GPUP_FMT_UNK = 0,
  GPUP_FMT_J2K,
  GPUP_FMT_JP2,
  GPUP_FMT_PXM,
  GPUP_FMT_PGX,
  GPUP_FMT_PAM,
  GPUP_FMT_BMP,
  GPUP_FMT_TIF,
  GPUP_FMT_RAW,
  GPUP_FMT_PNG,
  GPUP_FMT_RAWL,
  GPUP_FMT_JPG,
  GPUP_FMT_MJ2
} gpup_file_fmt;

typedef enum
{
  GPUP_CODEC_UNK = 0,
  GPUP_CODEC_J2K,
  GPUP_CODEC_JP2,
  GPUP_CODEC_MJ2
} gpup_codec_fmt;

typedef enum
{
  GPUP_RATE_CONTROL_BISECT,
  GPUP_RATE_CONTROL_PCRD_OPT
} gpup_rate_control;

/* ── Decode phase flags ──────────────────────────────────────── */

#define GPUP_DECODE_HEADER (1 << 0)
#define GPUP_DECODE_T2 (1 << 1)
#define GPUP_DECODE_T1 (1 << 2)
#define GPUP_DECODE_POST_T1 (1 << 3)
#define GPUP_DECODE_CLEAN (1 << 4)
#define GPUP_DECODE_ALL \
  (GPUP_DECODE_CLEAN | GPUP_DECODE_HEADER | GPUP_DECODE_T2 | GPUP_DECODE_T1 | GPUP_DECODE_POST_T1)

/* ── Plugin state flags ──────────────────────────────────────── */

#define GPUP_STATE_NO_DEBUG 0x0
#define GPUP_STATE_DEBUG 0x1
#define GPUP_STATE_PRE_TR1 0x2
#define GPUP_STATE_DWT_QUANTIZATION 0x4
#define GPUP_STATE_MCT_ONLY 0x8

/* ── Profile / Extension macros ──────────────────────────────── */

#define GPUP_PROFILE_NONE 0x0000
#define GPUP_PROFILE_0 0x0001
#define GPUP_PROFILE_1 0x0002
#define GPUP_PROFILE_CINEMA_2K 0x0003
#define GPUP_PROFILE_CINEMA_4K 0x0004
#define GPUP_PROFILE_CINEMA_S2K 0x0005
#define GPUP_PROFILE_CINEMA_S4K 0x0006
#define GPUP_PROFILE_CINEMA_LTS 0x0007
#define GPUP_PROFILE_BC_SINGLE 0x0100
#define GPUP_PROFILE_BC_MULTI 0x0200
#define GPUP_PROFILE_BC_MULTI_R 0x0300
#define GPUP_PROFILE_BC_MASK 0x030F
#define GPUP_PROFILE_IMF_2K 0x0400
#define GPUP_PROFILE_IMF_4K 0x0500
#define GPUP_PROFILE_IMF_8K 0x0600
#define GPUP_PROFILE_IMF_2K_R 0x0700
#define GPUP_PROFILE_IMF_4K_R 0x0800
#define GPUP_PROFILE_IMF_8K_R 0x0900
#define GPUP_PROFILE_MASK 0x0FFF
#define GPUP_PROFILE_PART2 0x8000
#define GPUP_PROFILE_PART2_EXTENSIONS_MASK 0x3FFF

#define GPUP_EXTENSION_NONE 0x0000
#define GPUP_EXTENSION_MCT 0x0100
#define GPUP_IS_PART2(v) ((v) & GPUP_PROFILE_PART2)
#define GPUP_IS_CINEMA(v) (((v) >= GPUP_PROFILE_CINEMA_2K) && ((v) <= GPUP_PROFILE_CINEMA_S4K))
#define GPUP_IS_STORAGE(v) ((v) == GPUP_PROFILE_CINEMA_LTS)
#define GPUP_IS_BROADCAST(v)                                                         \
  (((v) >= GPUP_PROFILE_BC_SINGLE) && ((v) <= (GPUP_PROFILE_BC_MULTI_R | 0x000b)) && \
   (((v) & 0xf) <= 0xb))
#define GPUP_IS_IMF(v)                                                          \
  (((v) >= GPUP_PROFILE_IMF_2K) && ((v) <= (GPUP_PROFILE_IMF_8K_R | 0x009b)) && \
   (((v) & 0xf) <= 0xb) && (((v) & 0xf0) <= 0x90))

/* ── Code block style macros ─────────────────────────────────── */

#define GPUP_CBLKSTY_LAZY 0x001
#define GPUP_CBLKSTY_RESET 0x002
#define GPUP_CBLKSTY_TERMALL 0x004
#define GPUP_CBLKSTY_VSC 0x008
#define GPUP_CBLKSTY_PTERM 0x010
#define GPUP_CBLKSTY_SEGSYM 0x020
#define GPUP_CBLKSTY_HT 0x040
#define GPUP_CBLKSTY_HT_MIXED 0x080
#define GPUP_CBLKSTY_HT_PHLD 0x100

/* ═══════════════════════════════════════════════════════════════
   Image types (minimal — zero-copy across host/plugin boundary)
   ═══════════════════════════════════════════════════════════════ */

typedef struct _gpup_image_comp
{
  uint32_t x0, y0;
  uint32_t w;
  uint32_t stride;
  uint32_t h;
  uint8_t dx, dy;
  uint8_t prec;
  bool sgnd;
  int32_t* data;
  bool owns_data;
} gpup_image_comp;

typedef struct _gpup_image
{
  uint32_t x0, y0, x1, y1;
  uint16_t numcomps;
  gpup_color_space color_space;
  gpup_image_comp* comps;
} gpup_image;

/* ═══════════════════════════════════════════════════════════════
   Plugin tile hierarchy
   ═══════════════════════════════════════════════════════════════ */

typedef struct _gpup_pass
{
  double distortionDecrease;
  size_t rate;
  size_t length;
} gpup_pass;

typedef struct _gpup_code_block
{
  /* debug info */
  uint32_t x0, y0, x1, y1;
  unsigned int* contextStream;
  /* data */
  uint32_t numPix;
  uint8_t* compressedData;
  uint32_t compressedDataLength;
  uint8_t numBitPlanes;
  size_t numPasses;
  gpup_pass passes[GPUP_MAX_PASSES];
  unsigned int sortedIndex;
} gpup_code_block;

typedef struct _gpup_precinct
{
  uint64_t numBlocks;
  gpup_code_block** blocks;
} gpup_precinct;

typedef struct _gpup_band
{
  uint8_t orientation;
  uint64_t numPrecincts;
  gpup_precinct** precincts;
  float stepsize;
} gpup_band;

typedef struct _gpup_resolution
{
  size_t level;
  size_t numBands;
  gpup_band** band;
} gpup_resolution;

typedef struct _gpup_tile_component
{
  size_t numResolutions;
  gpup_resolution** resolutions;
} gpup_tile_component;

typedef struct _gpup_tile
{
  uint32_t decompress_flags;
  size_t numComponents;
  gpup_tile_component** tileComponents;
} gpup_tile;

/* ═══════════════════════════════════════════════════════════════
   Header info (fields the plugin reads after host parses header)
   ═══════════════════════════════════════════════════════════════ */

typedef struct _gpup_header_info
{
  uint32_t cblockw_init;
  uint32_t cblockh_init;
  bool irreversible;
  uint8_t mct;
  uint16_t rsiz;
  uint8_t numresolutions;
  gpup_prog_order prog_order;
  uint8_t csty;
  uint8_t cblk_sty;
  uint32_t prcw_init[GPUP_MAXRLVLS];
  uint32_t prch_init[GPUP_MAXRLVLS];
  uint32_t tx0, ty0;
  uint32_t t_width, t_height;
  uint16_t t_grid_width, t_grid_height;
  uint16_t max_layers_;
} gpup_header_info;

/* ═══════════════════════════════════════════════════════════════
   Compress parameters (fields the plugin reads and/or writes)
   ═══════════════════════════════════════════════════════════════ */

typedef struct _gpup_compress_params
{
  bool tile_size_on;
  uint32_t tx0, ty0, t_width, t_height;
  uint16_t numlayers;
  bool allocationByRateDistoration;
  double layer_rate[GPUP_MAX_LAYERS];
  bool allocationByQuality;
  double layer_distortion[GPUP_MAX_LAYERS];
  uint8_t csty;
  uint8_t numgbits;
  gpup_prog_order prog_order;
  uint32_t numpocs;
  uint8_t numresolution;
  uint32_t cblockw_init;
  uint32_t cblockh_init;
  uint8_t cblk_sty;
  bool irreversible;
  int32_t roi_compno;
  uint32_t roi_shift;
  uint32_t res_spec;
  uint32_t prcw_init[GPUP_MAXRLVLS];
  uint32_t prch_init[GPUP_MAXRLVLS];
  char infile[GPUP_PATH_LEN];
  char outfile[GPUP_PATH_LEN];
  uint32_t image_offset_x0;
  uint32_t image_offset_y0;
  uint8_t subsampling_dx;
  uint8_t subsampling_dy;
  gpup_file_fmt decod_format;
  gpup_file_fmt cod_format;
  bool enableTilePartGeneration;
  uint8_t newTilePartProgressionDivider;
  uint8_t mct;
  uint64_t max_cs_size;
  uint64_t max_comp_size;
  uint16_t rsiz;
  uint16_t framerate;
  gpup_rate_control rateControlAlgorithm;
  uint32_t numThreads;
  int32_t deviceId;
  uint32_t duration;
  uint32_t kernelBuildOptions;
  uint32_t repeats;
  bool verbose;
  bool sharedMemoryInterface;
} gpup_compress_params;

/* ═══════════════════════════════════════════════════════════════
   Decompress parameters (fields the plugin reads)
   ═══════════════════════════════════════════════════════════════ */

typedef struct _gpup_decompress_core_params
{
  uint8_t reduce;
  uint16_t layers_to_decompress_;
} gpup_decompress_core_params;

typedef struct _gpup_decompress_params
{
  gpup_decompress_core_params core;
  char infile[GPUP_PATH_LEN];
  char outfile[GPUP_PATH_LEN];
  gpup_codec_fmt decod_format;
  gpup_file_fmt cod_format;
  double dw_x0, dw_y0, dw_x1, dw_y1;
  uint16_t tileIndex;
  int32_t deviceId;
  uint32_t kernelBuildOptions;
  uint32_t repeats;
  uint32_t numThreads;
  bool verbose_;
  void* user_data;
} gpup_decompress_params;

/* ═══════════════════════════════════════════════════════════════
   Init info
   ═══════════════════════════════════════════════════════════════ */

typedef struct _gpup_init_info
{
  int32_t deviceId;
  bool verbose;
  const char* license;
  const char* server;
} gpup_init_info;

/* ═══════════════════════════════════════════════════════════════
   Minimal stream params (for compress callback output)
   ═══════════════════════════════════════════════════════════════ */

typedef struct _gpup_stream_params
{
  const char* file;
  uint8_t* buf;
  size_t buf_len;
  size_t buf_compressed_len;
} gpup_stream_params;

/* ═══════════════════════════════════════════════════════════════
   Callback types and callback info structs
   ═══════════════════════════════════════════════════════════════ */

typedef int (*GPUP_INIT_DECOMPRESSORS)(gpup_header_info* header_info, gpup_image* image);

typedef struct _gpup_compress_callback_info
{
  const char* input_file_name;
  bool outputFileNameIsRelative;
  const char* output_file_name;
  gpup_compress_params* compressor_parameters;
  gpup_image* image;
  gpup_tile* tile;
  gpup_stream_params stream_params;
  unsigned int error_code;
  void* host_data;
} gpup_compress_callback_info;

typedef uint64_t (*GPUP_COMPRESS_USER_CALLBACK)(gpup_compress_callback_info* info);

typedef struct _gpup_compress_batch_info
{
  const char* input_dir;
  const char* output_dir;
  gpup_compress_params* compress_parameters;
  GPUP_COMPRESS_USER_CALLBACK callback;
} gpup_compress_batch_info;

typedef struct _gpup_decompress_callback_info
{
  size_t deviceId;
  GPUP_INIT_DECOMPRESSORS init_decompressors_func;
  const char* input_file_name;
  const char* output_file_name;
  gpup_codec_fmt decod_format;
  gpup_file_fmt cod_format;
  void* codec;
  gpup_header_info header_info;
  gpup_decompress_params* decompressor_parameters;
  gpup_image* image;
  bool plugin_owns_image;
  gpup_tile* tile;
  unsigned int error_code;
  uint32_t decompress_flags;
  uint32_t full_image_x0;
  uint32_t full_image_y0;
  void* user_data;
  void* format_private;
} gpup_decompress_callback_info;

typedef int32_t (*GPUP_DECOMPRESS_USER_CALLBACK)(gpup_decompress_callback_info* info);

/* ═══════════════════════════════════════════════════════════════
   Utility functions (inline, header-only)
   ═══════════════════════════════════════════════════════════════ */

#ifdef __cplusplus

static constexpr size_t gpup_buffer_alignment = GPUP_BUFFER_ALIGNMENT;

static inline uint32_t gpup_make_aligned_width(uint32_t width)
{
  return (uint32_t)((((uint64_t)width + GPUP_BUFFER_ALIGNMENT - 1) / GPUP_BUFFER_ALIGNMENT) *
                    GPUP_BUFFER_ALIGNMENT);
}

/*
 * Inline utility functions for image allocation/deallocation.
 * Guarded by GPUP_TYPES_ONLY so host codebases that poison malloc/free
 * can still include this header for the type definitions alone.
 */
#ifndef GPUP_TYPES_ONLY

inline gpup_image* gpup_image_new(uint16_t numcmpts, gpup_image_comp* cmptparms,
                                  gpup_color_space clrspc, bool alloc_data)
{
  auto* img = new gpup_image();
  memset(img, 0, sizeof(gpup_image));
  img->numcomps = numcmpts;
  img->color_space = clrspc;
  if(numcmpts > 0 && cmptparms)
  {
    img->comps = new gpup_image_comp[numcmpts]();
    for(uint16_t i = 0; i < numcmpts; i++)
    {
      img->comps[i] = cmptparms[i];
      if(alloc_data && cmptparms[i].w > 0 && cmptparms[i].h > 0)
      {
        auto stride = gpup_make_aligned_width(cmptparms[i].w);
        size_t dataSize = (size_t)stride * cmptparms[i].h * sizeof(int32_t);
        dataSize = ((dataSize + GPUP_BUFFER_ALIGNMENT - 1) / GPUP_BUFFER_ALIGNMENT) *
                   GPUP_BUFFER_ALIGNMENT;
        img->comps[i].data = (int32_t*)std::aligned_alloc(GPUP_BUFFER_ALIGNMENT, dataSize);
        if(img->comps[i].data)
          memset(img->comps[i].data, 0, dataSize);
        img->comps[i].stride = stride;
        img->comps[i].owns_data = true;
      }
    }
  }
  return img;
}

inline void gpup_image_destroy(gpup_image* img)
{
  if(!img)
    return;
  if(img->comps)
  {
    for(uint16_t i = 0; i < img->numcomps; i++)
    {
      if(img->comps[i].owns_data)
        std::free(img->comps[i].data);
    }
    delete[] img->comps;
  }
  delete img;
}

#endif /* GPUP_TYPES_ONLY */

#endif /* __cplusplus */
