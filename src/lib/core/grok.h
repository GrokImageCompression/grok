/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>

#ifndef SWIG
#ifdef __cplusplus
extern "C" {
#endif
#endif

#include "grk_config.h"

#ifndef SWIG
#ifdef _WIN32
#define GRK_CALLCONV __stdcall
#ifdef GRK_STATIC
#define GRK_API
#else
#ifdef GRK_EXPORTS
#define GRK_API __declspec(dllexport)
#else
#define GRK_API __declspec(dllimport)
#endif
#endif
#else
#define GRK_CALLCONV
#ifdef GRK_STATIC
#define GRK_API __attribute__((visibility("hidden")))
#else
#define GRK_API __attribute__((visibility("default")))
#endif
#endif
#else
#define GRK_CALLCONV
#define GRK_API
#endif

/**
 * @brief Environment variables
 *
 * @brief Set this environment variable to enable various levels of debug logging
 * levels: 1 - 5
 * level 1 provides only error logging
 * level 3 provides error, warning and information logging
 *
 * GRK_DEBUG
 *
 */

/**
 * @brief Progression orders
 */
typedef enum _GRK_PROG_ORDER
{
  GRK_PROG_UNKNOWN = -1, /** unknown progression order */
  GRK_LRCP = 0, /** layer-resolution-component-precinct order */
  GRK_RLCP = 1, /** resolution-layer-component-precinct order */
  GRK_RPCL = 2, /** resolution-precinct-component-layer order */
  GRK_PCRL = 3, /** precinct-component-resolution-layer order */
  GRK_CPRL = 4, /** component-precinct-resolution-layer order */
  GRK_NUM_PROGRESSION_ORDERS = 5 /** number of possible progression orders */
} GRK_PROG_ORDER;

/**
 * @brief Grok supported color spaces
 */
typedef enum _GRK_COLOR_SPACE
{
  GRK_CLRSPC_UNKNOWN = 0, /** unknown */
  GRK_CLRSPC_SRGB = 2, /** sRGB */
  GRK_CLRSPC_GRAY = 3, /** grayscale */
  GRK_CLRSPC_SYCC = 4, /** standard YCC (YUV) */
  GRK_CLRSPC_EYCC = 5, /** extended YCC */
  GRK_CLRSPC_CMYK = 6, /** CMYK */
  GRK_CLRSPC_DEFAULT_CIE = 7, /** default CIE LAB */
  GRK_CLRSPC_CUSTOM_CIE = 8, /** custom CIE LAB */
  GRK_CLRSPC_ICC = 9 /** ICC profile */
} GRK_COLOR_SPACE;

/**
 * @brief JPEG 2000 enumerated color spaces
 */
typedef enum _GRK_ENUM_COLOUR_SPACE
{
  GRK_ENUM_CLRSPC_UNKNOWN = 0xFFFFFFFF, /* unknown */
  GRK_ENUM_CLRSPC_BILEVEL1 = 0, /* bilevel 1 */
  GRK_ENUM_CLRSPC_YCBCR1 = 1, /* YCbCr 4:2:2 */
  GRK_ENUM_CLRSPC_YCBCR2 = 3, /* YCbCr 4:4:4 */
  GRK_ENUM_CLRSPC_YCBCR3 = 4, /* YCbCr 4:2:0 */
  GRK_ENUM_CLRSPC_PHOTO_YCC = 9, /* Kodak PhotoYCC */
  GRK_ENUM_CLRSPC_CMY = 11, /* cyan, magenta, yellow */
  GRK_ENUM_CLRSPC_CMYK = 12, /* cyan, magenta, yellow, black */
  GRK_ENUM_CLRSPC_YCCK = 13, /* YCCK */
  GRK_ENUM_CLRSPC_CIE = 14, /* CIE Lab (L*, a*, b*) */
  GRK_ENUM_CLRSPC_BILEVEL2 = 15, /* bilevel 2 */
  GRK_ENUM_CLRSPC_SRGB = 16, /* sRGB */
  GRK_ENUM_CLRSPC_GRAY = 17, /* grayscale */
  GRK_ENUM_CLRSPC_SYCC = 18, /* standard YCC */
  GRK_ENUM_CLRSPC_CIEJAB = 19, /* CIEJAB */
  GRK_ENUM_CLRSPC_ESRGB = 20, /* e-sRGB */
  GRK_ENUM_CLRSPC_ROMMRGB = 21, /* Reference Output Medium Metric RGB */
  GRK_ENUM_CLRSPC_YPBPR60 = 22, /* YPbPr 60 */
  GRK_ENUM_CLRSPC_YPBPR50 = 23, /* YPbPr 50 */
  GRK_ENUM_CLRSPC_EYCC = 24, /* extended YCC */
} GRK_ENUM_COLOUR_SPACE;

/**
 * @brief maximum Grok supported number of comments
 */
#define GRK_NUM_COMMENTS_SUPPORTED 256

/**
 * @brief maximum Grok supported number of asoc boxes
 */
#define GRK_NUM_ASOC_BOXES_SUPPORTED 256

/**
 * @brief maximum Grok supported comment length
 */
#define GRK_MAX_COMMENT_LENGTH (UINT16_MAX - 2)

/**
 * @brief maximum Grok supported precision
 */
#define GRK_MAX_SUPPORTED_IMAGE_PRECISION 16

/**
 *  @brief BIBO analysis - extra bits needed to avoid overflow:
 *  Lossless:
 *  without colour transform: 4 extra bits
 *  with colour transform:    5 extra bits
 *
 *  Lossy:
 *  1 extra bit
 *
 *  Worst-case scenario is lossless with colour transform :
 *  add 5 more bits to prec to avoid overflow. Add two more bits
 *  for good measure.
 *
 */
#define GRK_BIBO_EXTRA_BITS 7

/**
 * @brief Grok maximum number of passes
 */
#define GRK_MAX_PASSES (3 * (GRK_MAX_SUPPORTED_IMAGE_PRECISION + GRK_BIBO_EXTRA_BITS) - 2)

/**
 * @brief Logging callback
 * @param msg message
 * @param client_data client data passed to callback
 * */
typedef void (*grk_msg_callback)(const char* msg, void* client_data);

/**
 * @struct grk_msg_handlers
 * @brief Logging handlers
 * @param info_callback info callback (see @ref grk_msg_callback)
 * @param info_data info data
 * @param warn_callback warn callback (see @ref grk_msg_callback)
 * @param warn_data warn data
 * @param error_callback error callback (see @ref grk_msg_callback)
 * @param error_data error data
 */
typedef struct _grk_msg_handlers
{
  grk_msg_callback info_callback;
  void* info_data;
  grk_msg_callback debug_callback;
  void* debug_data;
  grk_msg_callback trace_callback;
  void* trace_data;
  grk_msg_callback warn_callback;
  void* warn_data;
  grk_msg_callback error_callback;
  void* error_data;
} grk_msg_handlers;

/**
 * @struct grk_object
 * @brief Opaque reference-counted object
 */
typedef struct _grk_object
{
  void* wrapper; /* opaque wrapper */
} grk_object;

/**
 * @struct grk_progression
 * @brief Progression order change (POC)
 */
typedef struct _grk_progression
{
  GRK_PROG_ORDER progression; /* progression */
  char progression_str[5]; /* progression as string */
  GRK_PROG_ORDER specified_compression_poc_prog; /* compression specified POC*/
  uint32_t tileno; /* tile number */

  /** tile dimensions */
  uint32_t tx0; /* tile x0 */
  uint32_t ty0; /* tile y0 */
  uint32_t tx1; /* tile x1 */
  uint32_t ty1; /* tile y1 */

  /** progression order bounds specified by POC */
  uint16_t comp_s; /* component start */
  uint16_t comp_e; /* component end */
  uint8_t res_s; /* resolution start */
  uint8_t res_e; /* resolution end */
  uint64_t prec_s; /* precinct start */
  uint64_t prec_e; /* precinct end */
  uint16_t lay_s; /* layer start */
  uint16_t lay_e; /* layer end */
  uint16_t tp_comp_s; /* tile part component start */
  uint16_t tp_comp_e; /* tile part component end */
  uint8_t tp_res_s; /* tile part resolution start */
  uint8_t tp_res_e; /* tile part resolution end */
  uint64_t tp_prec_e; /* tile part precinct end */
  uint16_t tp_lay_e; /* tile part layer end */
  uint32_t tp_tx_s; /* tile part x start */
  uint32_t tp_tx_e; /* tile part x end */
  uint32_t tp_ty_s; /* tile part y start */
  uint32_t tp_ty_e; /* tile part y end */
  uint32_t dx; /* dx */
  uint32_t dy; /* dy */

  /* temporary POC variables */
  uint16_t comp_temp; /* component */
  uint8_t res_temp; /* resolution */
  uint64_t prec_temp; /* precinct */
  uint16_t lay_temp; /* layer */
  uint32_t tx0_temp; /* x0 */
  uint32_t ty0_temp; /* y0 */
} grk_progression;

/**
 * @struct grk_raw_comp_cparameters
 * @brief RAW component compress parameters
 */
typedef struct _grk_raw_comp_cparameters
{
  uint8_t dx; /** subsampling in X direction */
  uint8_t dy; /** subsampling in Y direction */
} grk_raw_comp_cparameters;

/**
 * @struct grk_raw_cparameters
 * @brief RAW image compress parameters
 */
typedef struct _grk_raw_cparameters
{
  uint32_t width; /** width */
  uint32_t height; /** height */
  uint16_t numcomps; /** number of components */
  uint8_t prec; /** bit depth */
  bool sgnd; /** signed/unsigned */
  grk_raw_comp_cparameters* comps; /** component parameters array */
} grk_raw_cparameters;

/**
 * @brief Rate control algorithms
 * @param GRK_RATE_CONTROL_BISECT: bisect with all truncation points
 * @param GRK_RATE_CONTROL_PCRD_OPT: PCRD: bisect with only feasible truncation points
 */
typedef enum _GRK_RATE_CONTROL_ALGORITHM
{
  GRK_RATE_CONTROL_BISECT,
  GRK_RATE_CONTROL_PCRD_OPT
} GRK_RATE_CONTROL_ALGORITHM;

/**
 * @brief All Grok supported file formats
 */
typedef enum _GRK_SUPPORTED_FILE_FMT
{
  GRK_FMT_UNK, /* unknown */
  GRK_FMT_J2K, /* J2K */
  GRK_FMT_JP2, /* JP2 */
  GRK_FMT_PXM, /* PXM */
  GRK_FMT_PGX, /* PGX */
  GRK_FMT_PAM, /* PAM */
  GRK_FMT_BMP, /* BMP */
  GRK_FMT_TIF, /* TIF */
  GRK_FMT_RAW, /* RAW Big Endian */
  GRK_FMT_PNG, /* PNG */
  GRK_FMT_RAWL, /* RAW Little Endian */
  GRK_FMT_JPG, /* JPG */
  GRK_FMT_YUV, /* YUV */
  GRK_FMT_MJ2 /* MJ2 */
} GRK_SUPPORTED_FILE_FMT;

/**
 * @brief Grok Supported JPEG 2000 formats
 */
typedef enum _GRK_CODEC_FORMAT
{
  GRK_CODEC_UNK, /** unknown format */
  GRK_CODEC_J2K, /** JPEG 2000 code-stream */
  GRK_CODEC_JP2, /** JPEG 2000 JP2 file format */
  GRK_CODEC_MJ2 /** Motion JPEG 2000 */
} GRK_CODEC_FORMAT;

#define GRK_PATH_LEN 4096 /* Grok maximum supported filename size */
#define GRK_MAX_LAYERS 256 /* Grok maximum number of quality layers */

/*
 * Note: range for number of decomposition levels is 0-32
 * Accordingly, range for number of resolution levels is 1-33
 */
#define GRK_MAX_DECOMP_LVLS                                        \
  32 /* Maximum number of decomposition levels allowed by standard \
      */
#define GRK_MAXRLVLS \
  (GRK_MAX_DECOMP_LVLS + 1) /* Maximum number of resolution levels allowed by standard*/
#define GRK_MAXBANDS (3 * GRK_MAXRLVLS - 2) /*  Maximum number of sub-bands allowed by standard */

/**
 * @struct grk_component_mapping_comp
 * @brief Component mappings: component index, mapping type, palette column
 * Note: "component" refers to an image component as decompressed
 * from the code stream, while "channel" refers to a component resulting
 * from the application of a Palette box LUT and a Component mapping box.
 */
typedef struct _grk_component_mapping_comp
{
  uint16_t component; /* component index */
  uint8_t mapping_type; /* mapping type : 1 if mapped to paletted LUT otherwise 0 */
  uint8_t palette_column; /* palette LUT column if mapped to palette */
} grk_component_mapping_comp;

/**
 * @struct grk_palette_data
 * @brief Palette data
 */
typedef struct _grk_palette_data
{
  int32_t* lut; /* LUT */
  uint16_t num_entries; /* number of LUT entries */
  grk_component_mapping_comp* component_mapping; /* component mapping array*/
  uint8_t num_channels; /* number of channels */
  bool* channel_sign; /* channel signed array */
  uint8_t* channel_prec; /* channel precision array */
} grk_palette_data;

/***
 * Channel Definition box structures and enums.
 * When no Component mapping box is present, it is still possible to have
 * a Channel definition box, in which case channels are associated with components
 * in the obvious way : channel `k` corresponds to component `k`.
 * */

/* @brief Channel type */
typedef enum _GRK_CHANNEL_TYPE
{
  GRK_CHANNEL_TYPE_COLOUR = 0, /* colour */
  GRK_CHANNEL_TYPE_OPACITY = 1, /* opacity */
  GRK_CHANNEL_TYPE_PREMULTIPLIED_OPACITY = 2, /* premultiplied opacity */
  GRK_CHANNEL_TYPE_UNSPECIFIED = 65535U /* unspecified */
} GRK_CHANNEL_TYPE;

/**
 * @brief Channel association
 */
typedef enum _GRK_CHANNEL_ASSOC
{
  GRK_CHANNEL_ASSOC_WHOLE_IMAGE = 0, /* whole image */
  GRK_CHANNEL_ASSOC_COLOUR_1 = 1, /* colour 1 */
  GRK_CHANNEL_ASSOC_COLOUR_2 = 2, /* colour 2 */
  GRK_CHANNEL_ASSOC_COLOUR_3 = 3, /* colour 3 */
  GRK_CHANNEL_ASSOC_UNASSOCIATED = 65535U /* unassociated */
} GRK_CHANNEL_ASSOC;

/**
 * @struct grk_channel_description
 * @brief Channel definition: channel index, type, association
 */
typedef struct _grk_channel_description
{
  uint16_t channel; /* channel */
  uint16_t typ; /* type */
  uint16_t asoc; /* association */
} grk_channel_description;

/**
 * @struct grk_channel_definition
 * @brief Channel definition
 */
typedef struct _grk_channel_definition
{
  grk_channel_description* descriptions; /* channel description array */
  uint16_t num_channel_descriptions; /* size of channel description array */
} grk_channel_definition;

/**
 * @struct grk_asoc
 * @brief Association box info
 */
typedef struct _grk_asoc
{
  uint32_t level; /* level: 0 for root level */
  const char* label; /* label */
  uint8_t* xml; /* xml */
  uint32_t xml_len; /* xml length */
} grk_asoc;

/**
 * @brief Precision mode
 */
typedef enum _grk_precision_mode
{
  GRK_PREC_MODE_CLIP, /* clip */
  GRK_PREC_MODE_SCALE /* scale */
} grk_precision_mode;

/**
 * @struct grk_precision
 * @brief Precision
 */
typedef struct _grk_precision
{
  uint8_t prec; /* precision */
  grk_precision_mode mode; /* mode */
} grk_precision;

/**
 * @struct grk_progression_state
 * @brief Stores progression state information
 * Note: limited to 256 components
 */
typedef struct _grk_progression_state
{
  uint8_t num_resolutions;
  uint16_t layers_per_resolution[33];
  uint16_t numcomps;
  uint16_t comp[256];
  bool single_tile;
  uint16_t tile_index;
} grk_progression_state;

/**
 * @struct grk_io_buf
 * @brief Grok IO buffer
 */
typedef struct _grk_io_buf
{
  uint8_t* data; /* data */
  size_t offset; /* offset */
  size_t len; /* length */
  size_t alloc_len; /* allocated length */
  bool pooled; /* pooled */
  uint32_t index; /* index */
} grk_io_buf;

/**
 * @struct grk_io_init
 * @brief Grok IO initialization
 */
typedef struct _grk_io_init
{
  uint32_t max_pooled_requests; /* max pooled requests */

} grk_io_init;

/**
 * @brief Grok IO callback
 * @param worker_id worker id
 * @param buffer io buffer (see @ref grk_io_buf)
 * @param io_user_data user data
 */
typedef bool (*grk_io_callback)(uint32_t worker_id, grk_io_buf buffer, void* io_user_data);

/**
 * @brief Grok IO register reclaim callback
 * @param io_init io initialization (see @ref grk_io_init)
 * @param reclaim_callback (see @ref grk_io_callback)
 * @param io_user_data io user data
 * @param reclaim_user_data reclaim user data
 */
typedef void (*grk_io_register_reclaim_callback)(grk_io_init io_init,
                                                 grk_io_callback reclaim_callback,
                                                 void* io_user_data, void* reclaim_user_data);

/**
 * @brief Grok IO pixels callback
 * @param worker_id worker id
 * @param buffer Grok io buffer (see @ref grk_io_buf)
 * @param user_data user data
 */
typedef bool (*grk_io_pixels_callback)(uint32_t worker_id, grk_io_buf buffer, void* user_data);

/**
 * @brief Read stream callback
 * @param buffer buffer to write stream to
 * @param numBytes number of bytes to write to buffer
 * @param user_data user data
 *
 */
typedef size_t (*grk_stream_read_fn)(uint8_t* buffer, size_t numBytes, void* user_data);

/**
 * @brief Write stream callback
 * @param buffer buffer to read stream from
 * @param numBytes number of bytes to read from buffer
 * @param user_data user data
 *
 */
typedef size_t (*grk_stream_write_fn)(const uint8_t* buffer, size_t numBytes, void* user_data);

/**
 * @brief Seek (absolute) callback
 * @param offset 			absolute stream offset
 * @param user_data user 	data
 */
typedef bool (*grk_stream_seek_fn)(uint64_t offset, void* user_data);

/**
 * @brief Free user data callback
 * @param user_data user data
 */
typedef void (*grk_stream_free_user_data_fn)(void* user_data);

/**
 * @struct grk_stream_params
 * @brief JPEG 2000 stream parameters
 * There are three methods of streaming: by file, buffer or callback
 * Client must choose only one method, and populate this struct accordingly
 */
typedef struct _grk_stream_params
{
  /* 0. General Streaming */
  size_t initial_offset; /* initial offset into stream */
  size_t double_buffer_len; /* length of internal double buffer
                               for stdio and callback streaming */
  size_t initial_double_buffer_len; /* choose a larger initial length
                                       to read the main header in one go */
  bool from_network; /* indicates stream source is on network if true */
  bool is_read_stream; /* true if read stream, otherwise false */

  /* 1. File Streaming */
  char file[GRK_PATH_LEN];
  bool use_stdio; /* use C file api - if false then use memory mapping */

  /* 2. Buffer Streaming */
  uint8_t* buf; /* data buffer */
  size_t buf_len; /* data buffer length */
  size_t buf_compressed_len; /* length of compressed stream (set by compressor, not client) */

  /* 3. Callback Streaming */
  grk_stream_read_fn read_fn; /* read function */
  grk_stream_write_fn write_fn; /* write function */
  grk_stream_seek_fn seek_fn; /* seek function */
  grk_stream_free_user_data_fn free_user_data_fn; /* optional */
  void* user_data; /* user data */
  size_t stream_len; /* mandatory for read stream */

  /* 4 Authorization */
  char username[129]; /* AWS access key ID: max 128 chars + null */
  char password[129]; /* Secret access key: max 128 chars + null */
  char bearer_token[4097]; /* Session token: up to 4096 chars + null */
  char custom_header[1024]; /* Single header: fits within 8KB total limit */
  char region[64]; /* Region code: max 50 chars + buffer */

  /* 5 S3 Endpoint Configuration (for S3-compatible services like MinIO) */
  char s3_endpoint[256]; /* Custom S3 endpoint URL (e.g. "http://localhost:9000") */
  int8_t s3_use_https; /* 0 = auto (default), 1 = HTTPS, -1 = HTTP */
  int8_t s3_use_virtual_hosting; /* 0 = auto (default), 1 = virtual-hosted, -1 = path-style */
  bool s3_no_sign_request; /* true = skip authentication (public buckets) */
  bool s3_allow_insecure; /* true = disable SSL certificate verification */

} grk_stream_params;

/**
 * @brief Grok tile cache strategy
 */
#define GRK_TILE_CACHE_NONE 0 /* no tile caching */
#define GRK_TILE_CACHE_IMAGE 1 /* cache final tile image */
#define GRK_TILE_CACHE_ALL 2 /* cache everything */
#define GRK_TILE_CACHE_LRU 4 /* LRU: release decompressed data, keep processor */

typedef struct _grk_image grk_image;

/**
 * @brief Callback called when decompression of a tile has completed
 */
typedef void (*grk_decompress_callback)(void* codec, uint16_t tile_index, grk_image* tile_image,
                                        uint8_t reduction, void* user_data);

/**
 * Decompression: disable random access markers
 */
#define GRK_RANDOM_ACCESS_PLT 1 /* Disable PLT marker if present */
#define GRK_RANDOM_ACCESS_TLM 2 /* Disable TLM marker if present */
#define GRK_RANDOM_ACCESS_PLM 4 /* Disable PLM marker if present */

/**
 * @struct grk_decompress_core_params
 * @brief Core decompression parameters
 */
typedef struct _grk_decompress_core_params
{
  /**
   * Set the number of highest resolution levels to be discarded.
   * The image resolution is effectively divided by 2 to the power of the number of discarded
   * levels. The reduce factor is limited by the smallest total number of decomposition levels
   * across tiles. If value is greater than zero, then the image is decoded to original dimension
   * divided by 2^(reduce). If the value is equal to zero or not set, then the image is decompressed
   * at full resolution
   */
  uint8_t reduce;
  /**
   * Set the maximum number of quality layers to decompress.
   * If there are fewer quality layers than the specified number, all quality layers will be
   * decompressed. If value is non-zero, then only these specified layers are decompressed.
   * If value is zero or not set, then all the quality layers are decompressed
   */
  uint16_t layers_to_decompress;
  uint32_t tile_cache_strategy; /* tile cache strategy */
  uint32_t disable_random_access_flags; /* disable random access flags */
  bool skip_allocate_composite; /* skip allocate composite image data for multi-tile */
  grk_io_pixels_callback io_buffer_callback; /* IO buffer callback */
  void* io_user_data; /* IO user data */
  grk_io_register_reclaim_callback io_register_client_callback; /* IO register client callback */
} grk_decompress_core_params;

/**
 * @brief default compression level for decompression output file formats
 * that support compression
 */
#define GRK_DECOMPRESS_COMPRESSION_LEVEL_DEFAULT (UINT_MAX)

/**
 * @struct grk_decompress_parameters
 * @brief Decompression parameters
 */
typedef struct _grk_decompress_params
{
  grk_decompress_core_params core; /* core parameters */
  bool asynchronous; /* if true then decompression is executed asynchronously */
  bool simulate_synchronous;
  grk_decompress_callback decompress_callback; /* callback for asynchronous decompression */
  void* decompress_callback_user_data; /* user data passed to callback for asynchronous
                                          decompression */
  char infile[GRK_PATH_LEN]; /* input file */
  char outfile[GRK_PATH_LEN]; /* output file */
  GRK_CODEC_FORMAT decod_format; /* input decode format */
  GRK_SUPPORTED_FILE_FMT cod_format; /* output code format */
  double dw_x0; /* decompress window left boundary*/
  double dw_x1; /* decompress window right boundary*/
  double dw_y0; /* decompress window top boundary*/
  double dw_y1; /* decompress window bottom boundary*/
  uint16_t tile_index; /* index of decompressed tile*/

  /***************************************************************************
     Note: when using Grok API, the following parameters should be set
     on the HeaderInfo struct before reading the header. Otherwise they
     will be ignored
  */
  GRK_COLOR_SPACE color_space;
  bool force_rgb; /* force output to sRGB */
  bool upsample; /* upsample components according to their dx and dy values*/
  grk_precision* precision; /* precision array */
  uint32_t num_precision; /* size of precision array*/
  bool split_by_component; /* split output components to different files for PNM */
  bool single_tile_decompress; /* single tile decompress */
  /************************************************************************** */

  bool io_xml; /* serialize XML metedata to disk*/
  uint32_t compression; /* compression */
  uint32_t compression_level; /* compression "quality" - meaning depends on output file format */
  uint32_t duration; /* duration of decompression in seconds */
  uint32_t repeats; /* number of repetitions */
  uint32_t num_threads; /* number of CPU threads */

  uint32_t kernel_build_options; /* plugin OpenCL kernel build options */
  int32_t device_id; /* plugin device ID */
  void* user_data; /* plugin user data */
} grk_decompress_parameters;

/**
 * @brief Grok Data types
 * Used to specify the actual data type of Grok image components
 *
 */
typedef enum _grk_data_type
{
  GRK_INT_32,
  GRK_INT_16,
  GRK_INT_8,
  GRK_FLOAT,
  GRK_DOUBLE
} grk_data_type;

/**
 * @struct grk_image_comp
 * @brief Image component
 */
typedef struct _grk_image_comp
{
  uint32_t x0; /* x offset relative to whole image */
  uint32_t y0; /* y offset relative to whole image */
  uint32_t w; /* width */
  uint32_t stride; /* stride */
  uint32_t h; /* height */
  uint8_t dx; /* horizontal separation of component samples with respect to reference grid  */
  uint8_t dy; /* vertical separation of component samples with respect to reference grid  */
  uint8_t prec; /* precision */
  bool sgnd; /* true if component data is signed */
  GRK_CHANNEL_TYPE type; /* channel type */
  GRK_CHANNEL_ASSOC association; /* channel association */
  uint16_t crg_x; /* component registration x coordinate */
  uint16_t crg_y; /* component registration y coordinate */
  grk_data_type data_type;
  void* data; /* component data */
  bool owns_data; /* true if data is owned by component */
} grk_image_comp;

/**
 * @struct grk_color
 * @brief ICC profile, palette, channel definition
 */
typedef struct _grk_color
{
  uint8_t* icc_profile_buf; /* ICC profile buffer */
  uint32_t icc_profile_len; /* ICC profile length */
  char* icc_profile_name; /* ICC profile name */
  grk_channel_definition* channel_definition; /* channel definition */
  grk_palette_data* palette; /* palette */
  bool has_colour_specification_box; /* true if colour specification box present*/
} grk_color;

/**
 * @struct grk_image_data
 * @brief Image meta data: colour, IPTC and XMP
 */
typedef struct _grk_image_meta
{
  grk_object obj; /* object */
  grk_color color; /* color */
  uint8_t* iptc_buf; /* IPTC buffer */
  size_t iptc_len; /* IPTC length */
  uint8_t* xmp_buf; /* XMP buffer */
  size_t xmp_len; /* XMP length */
} grk_image_meta;

/**
 * @struct grk_image
 * @brief Grok image
 * Note: do not directly create a grk_image object. Instead use the @ref grk_image_new
 * API method to create a grk_image object, and clean it up with @ref grk_object_unref
 */
typedef struct _grk_image
{
  grk_object obj; /* object */
  uint32_t x0; /* image horizontal offset from origin of reference grid */
  uint32_t y0; /* image vertical offset from origin of reference grid */
  uint32_t x1; /* image right boundary in reference grid*/
  uint32_t y1; /* image bottom boundary in reference grid */
  uint16_t numcomps; /* number of components */
  GRK_COLOR_SPACE color_space; /* color space */
  bool palette_applied; /* true if palette applied */
  bool channel_definition_applied; /* true if channel definition applied */
  bool has_capture_resolution; /* true if capture resolution present*/
  double capture_resolution[2]; /* capture resolution */
  bool has_display_resolution; /* true if display resolution present*/
  double display_resolution[2]; /* display resolution */
  GRK_SUPPORTED_FILE_FMT decompress_fmt; /* decompress format */
  bool force_rgb; /* force RGB */
  bool upsample; /* upsample */
  grk_precision* precision; /* precision */
  uint32_t num_precision; /* number of precision */
  bool has_multiple_tiles; /* has multiple tiles */
  bool split_by_component; /* split by component */
  uint16_t decompress_num_comps; /* decompress number of components */
  uint32_t decompress_width; /* decompress width */
  uint32_t decompress_height; /* decompress height */
  uint8_t decompress_prec; /* decompress precision */
  GRK_COLOR_SPACE decompress_colour_space; /* decompress colour space */
  grk_io_buf interleaved_data; /* interleaved data */
  uint32_t rows_per_strip; /* for storage to output format */
  uint32_t rows_per_task; /* for scheduling */
  uint64_t packed_row_bytes; /* packed row bytes */
  grk_image_meta* meta; /* image meta data */
  grk_image_comp* comps; /* components array */
} grk_image;

/**
 * @struct grk_header_info
 * @brief JPEG 2000 header info
 */
typedef struct _grk_header_info
{
  /************************************************************
  Variables below are populated by library after reading header
  *************************************************************/
  grk_image header_image;
  uint32_t cblockw_init; /* nominal code block width, default 64 */
  uint32_t cblockh_init; /* nominal code block height, default 64 */
  bool irreversible; /* true if image compressed irreversibly*/
  uint8_t mct; /* multi-component transform */
  uint16_t rsiz; /* RSIZ */
  uint8_t numresolutions; /* number of resolutions */
  GRK_PROG_ORDER prog_order; /* progression order */
  /********************************************************************************
  coding style
  Can be specified in main header COD segment,
  tile header COD segment, and tile component COC segment.
   Important note: we assume that coding style does not vary across tile components
  *********************************************************************************/
  uint8_t csty;
  /************************************************************************************
  code block style
  Can be specified in main header COD segment, and can   be overridden in a tile header.
  Important note: we assume that code block style does not vary across tiles
  ************************************************************************************/
  uint8_t cblk_sty;
  uint32_t prcw_init[GRK_MAXRLVLS]; /* nominal precinct width */
  uint32_t prch_init[GRK_MAXRLVLS]; /* nominal precinct height */
  uint32_t tx0; /* XTOsiz */
  uint32_t ty0; /* YTOsiz */
  uint32_t t_width; /* XTsiz */
  uint32_t t_height; /* YTsiz */
  uint16_t t_grid_width; /* tile grid width */
  uint16_t t_grid_height; /* tile grid height */
  uint16_t num_layers; /* number of layers */
  uint8_t* xml_data; /* XML data - will remain valid until codec destroyed */
  size_t xml_data_len; /* XML data length */
  size_t num_comments; /* number of comments */
  char* comment[GRK_NUM_COMMENTS_SUPPORTED]; /* comment */
  uint16_t comment_len[GRK_NUM_COMMENTS_SUPPORTED]; /* comment length */
  bool is_binary_comment[GRK_NUM_COMMENTS_SUPPORTED]; /* is binary comment */
  grk_asoc asocs[GRK_NUM_ASOC_BOXES_SUPPORTED]; /* associations */
  uint32_t num_asocs; /* number of associations */

  /********************************
  Variables below are set by client
  ********************************/
  GRK_COLOR_SPACE color_space;
  GRK_SUPPORTED_FILE_FMT decompress_fmt; /* decompress format */
  bool force_rgb; /* force RGB */
  bool upsample; /* upsample */
  grk_precision* precision; /* precision */
  uint32_t num_precision; /* number of precision */
  bool split_by_component; /* split by component */
  bool single_tile_decompress; /* single tile decompress */
} grk_header_info;

/**
 * @struct grk_swath_buffer
 * @brief User-managed output buffer for asynchronous swath tile copy-and-convert.
 *
 * After grk_decompress_wait() returns for a given swath, pass this struct to
 * grk_decompress_schedule_swath_copy() to schedule per-tile copies from the
 * internal int32_t tile buffers into the caller's output buffer via Taskflow
 * and Highway SIMD.
 *
 * The output buffer layout mirrors GDAL's pixel/line/band spacing convention,
 * supporting BIP, BSQ, or any other interleaving.  Output type is described by
 * prec (bit depth: 8, 16, or 32) and sgnd (signed vs unsigned):
 *   prec=8,  sgnd=false  → uint8_t   (GDT_Byte)
 *   prec=16, sgnd=false  → uint16_t  (GDT_UInt16)
 *   prec=16, sgnd=true   → int16_t   (GDT_Int16)
 *   prec=32, sgnd=false  → uint32_t  (GDT_UInt32)
 *   prec=32, sgnd=true   → int32_t   (GDT_Int32)
 *
 * Conversion from the internal int32_t source follows GDALCopyWords
 * semantics: values are clamped to the output type's representable range.
 *
 * band_map[i] is a 1-based component index (GDAL panBandMap convention):
 * output band i is sourced from image component band_map[i]-1.
 * If band_map is NULL, band i is sourced from component i.
 *
 * promote_alpha: if >= 0, the component at this 0-based index is a 1-bit
 * alpha channel and its values are scaled by 255 before output.
 *
 * Assumes component subsampling dx == dy == 1.
 * The user must keep the buffer alive until grk_decompress_wait_swath_copy().
 */
typedef struct grk_swath_buffer
{
  void* data; /**< Output buffer pointer */
  uint8_t prec; /**< Output bit depth: 8, 16, or 32 */
  bool sgnd; /**< true = signed output (int16/int32); false = unsigned */
  uint16_t numcomps; /**< Number of output bands */
  int64_t pixel_space; /**< Bytes between adjacent pixels in same row and band (GDAL nPixelSpace) */
  int64_t line_space; /**< Bytes between adjacent rows in same band (GDAL nLineSpace) */
  int64_t band_space; /**< Bytes between adjacent bands (GDAL nBandSpace) */
  int* band_map; /**< numcomps-element array of 1-based component indices, or NULL */
  int promote_alpha; /**< 0-based component index for 1-bit alpha promotion, or -1 */
  uint32_t x0; /**< Swath image x origin (image pixel coordinates) */
  uint32_t y0; /**< Swath image y origin (image pixel coordinates) */
  uint32_t x1; /**< Swath image x end, exclusive (image pixel coordinates) */
  uint32_t y1; /**< Swath image y end, exclusive (image pixel coordinates) */
} grk_swath_buffer;

/**
 * @struct grk_wait_swath
 * @brief Specify swath region to wait on during asynchronous decompression.
 *
 * Input: Set pixel coordinates (x0, y0, x1, y1) before calling grk_decompress_wait().
 * Output: After grk_decompress_wait() returns, tile_x0/y0/x1/y1 and num_tile_cols
 *         are populated with the tile grid indices covering the requested swath.
 *         These can be used with grk_decompress_get_tile_image() to retrieve
 *         per-tile decoded data.
 *
 * Note: tile coordinates are only populated when grk_decompress_wait() is called
 * with a non-null swath pointer and asynchronous+simulate_synchronous mode is enabled.
 */
typedef struct grk_wait_swath
{
  uint32_t x0; /**< Input: Pixel coordinate: top-left x */
  uint32_t y0; /**< Input: Pixel coordinate: top-left y */
  uint32_t x1; /**< Input: Pixel coordinate: bottom-right x (exclusive) */
  uint32_t y1; /**< Input: Pixel coordinate: bottom-right y (exclusive) */
  uint16_t tile_x0; /**< Output: Global tile column start (inclusive) */
  uint16_t tile_y0; /**< Output: Global tile row start (inclusive) */
  uint16_t tile_x1; /**< Output: Global tile column end (exclusive) */
  uint16_t tile_y1; /**< Output: Global tile row end (exclusive) */
  uint16_t num_tile_cols; /**< Output: Total tile columns in the image tile grid */
} grk_wait_swath;
/**
 * @struct grk_plugin_pass
 * @brief Plugin pass
 */
typedef struct _grk_plugin_pass
{
  double distortion_decrease; /* distortion decrease up to and including this pass */
  size_t rate; /* rate up to and including this pass */
  size_t length; /* stream length for this pass */
} grk_plugin_pass;

/**
 * @struct grk_plugin_code_block
 * @brief Plugin code block
 */
typedef struct _grk_plugin_code_block
{
  /**************************
  debug info
  **************************/
  uint32_t x0, y0, x1, y1; /* x0, y0, x1, y1 */
  unsigned int* context_stream; /* context stream */
  /***************************/

  uint32_t num_pix; /* number of pixels */
  uint8_t* compressed_data; /* compressed data */
  uint32_t compressed_data_length; /* compressed data length */
  uint8_t num_bit_planes; /* number of bit planes */
  uint8_t num_passes; /* number of passes */
  grk_plugin_pass passes[GRK_MAX_PASSES]; /* passes */
  unsigned int sorted_index; /* sorted index */
} grk_plugin_code_block;

/**
 * @brief grk_plugin_precinct
 * @brief Plugin precinct
 */
typedef struct _grk_plugin_precinct
{
  uint64_t num_blocks; /* number of blocks */
  grk_plugin_code_block** blocks; /* blocks */
} grk_plugin_precinct;

/**
 * @struct grk_plugin_band
 * @brief Plugin band
 */
typedef struct _grk_plugin_band
{
  uint8_t orientation; /* orientation */
  uint64_t num_precincts; /* number of precincts */
  grk_plugin_precinct** precincts; /* precincts */
  float stepsize; /* stepsize */
} grk_plugin_band;

/**
 * @struct grk_plugin_resolution
 * @brief Plugin resolution
 */
typedef struct _grk_plugin_resolution
{
  uint8_t level; /* level */
  uint8_t num_bands; /* number of bands */
  grk_plugin_band** band; /* band */
} grk_plugin_resolution;

/**
 * @struct grk_plugin_tile_component
 * @brief Plugin tile component
 */
typedef struct grk_plugin_tile_component
{
  uint8_t numresolutions; /* number of resolutions */
  grk_plugin_resolution** resolutions; /* resolutions */
} grk_plugin_tile_component;

#define GRK_DECODE_HEADER (1 << 0)
#define GRK_DECODE_T2 (1 << 1)
#define GRK_DECODE_T1 (1 << 2)
#define GRK_DECODE_POST_T1 (1 << 3)
#define GRK_PLUGIN_DECODE_CLEAN (1 << 4)
#define GRK_DECODE_ALL \
  (GRK_PLUGIN_DECODE_CLEAN | GRK_DECODE_HEADER | GRK_DECODE_T2 | GRK_DECODE_T1 | GRK_DECODE_POST_T1)

/**
 * @struct grk_plugin_tile
 * @brief Plugin tile
 */
typedef struct _grk_plugin_tile
{
  uint32_t decompress_flags; /* decompress flags */
  uint16_t num_components; /* number of components */
  grk_plugin_tile_component** tile_components; /* tile components */
} grk_plugin_tile;

/**
 * @brief Gets the Grok library version string.
 *
 * Returns a null-terminated string of the form "MAJOR.MINOR.PATCH"
 * (e.g. "10.0.5").  The string is statically allocated; do not free it.
 *
 * @return null-terminated version string
 */
GRK_API const char* GRK_CALLCONV grk_version(void);

/**
 * @brief Initializes the Grok library.
 *
 * Must be called once before any other Grok API function.  It is safe to call
 * multiple times; subsequent calls are no-ops unless the library was shut down.
 *
 * Sets up the Taskflow thread pool used by all async decompress/compress
 * operations. Pass @p num_threads = 0 to use all available logical CPUs.
 * The thread pool persists for the lifetime of the process.
 *
 * @param plugin_path       path to an optional hardware-accelerator plugin .so;
 *                          pass NULL for CPU-only operation
 * @param num_threads       number of worker threads (0 = use all CPUs)
 * @param plugin_initialized if non-NULL, set to true when a plugin was loaded
 *                          and initialized successfully, false otherwise
 */
GRK_API void GRK_CALLCONV grk_initialize(const char* plugin_path, uint32_t num_threads,
                                         bool* plugin_initialized);

/**
 * @brief Increments the reference count on a Grok object.
 *
 * Call this when you want to share ownership of a codec or image object
 * across multiple owners.  Each call to grk_object_ref() must be matched
 * by exactly one call to grk_object_unref().
 *
 * @param obj Grok object (see @ref grk_object); passing NULL is a no-op
 * @return the same @p obj pointer (for convenience), or NULL if obj is NULL
 */
GRK_API grk_object* GRK_CALLCONV grk_object_ref(grk_object* obj);

/**
 * @brief Decrements the reference count on a Grok object.
 *
 * When the reference count reaches zero the object (codec or image) is
 * destroyed and all associated decompressor/compressor resources are freed.
 * The caller must not access the object after this call if its count reaches
 * zero.
 *
 * For a decompressor codec: completes any in-flight async tasks before
 * destroying the object.  For an image: frees component data buffers.
 *
 * @param obj Grok object (see @ref grk_object); passing NULL is a no-op
 */
GRK_API void GRK_CALLCONV grk_object_unref(grk_object* obj);

/**
 * @brief Installs application-defined log message handlers.
 *
 * Replaces the default handlers (which print to stderr/stdout) with
 * user-supplied callbacks for info, warning, and error messages.
 * Call before grk_initialize() or at any point to change handlers.
 * Passing a NULL function pointer for any handler restores the default
 * behaviour for that level.
 *
 * @param msg_handlers struct of {info, warn, error} function pointers
 *                     (see @ref grk_msg_handlers)
 */
GRK_API void GRK_CALLCONV grk_set_msg_handlers(grk_msg_handlers msg_handlers);

/**
 * @brief Allocates a new Grok image with the specified component layout.
 *
 * Creates an image whose component geometry is described by @p cmptparms.
 * Each entry in @p cmptparms specifies width, height, bit-depth, signedness,
 * and sub-sampling factors for one component.
 *
 * When @p alloc_data is true, a contiguous int32_t data block is allocated
 * for every component; when false, the component data pointers are NULL and
 * the caller must supply them before passing the image to grk_compress_init().
 *
 * The returned image is reference-counted.  Release with grk_object_unref().
 *
 * @param numcmpts   number of components (e.g. 1 for grayscale, 3 for RGB)
 * @param cmptparms  per-component parameters array — length must be @p numcmpts
 *                   (see @ref grk_image_comp)
 * @param clrspc     image colour space (e.g. GRK_CLRSPC_SRGB, GRK_CLRSPC_GRAY
 *                   — see @ref GRK_COLOR_SPACE)
 * @param alloc_data if true, allocate int32_t data buffers for all components
 * @return pointer to newly allocated @ref grk_image, or NULL on failure
 */
GRK_API grk_image* GRK_CALLCONV grk_image_new(uint16_t numcmpts, grk_image_comp* cmptparms,
                                              GRK_COLOR_SPACE clrspc, bool alloc_data);

/**
 * @brief Allocates an empty image metadata object.
 *
 * Returns a heap-allocated @ref grk_image_meta that holds optional metadata
 * (ICC profile, XMP, IPTC, EXIF) to associate with an image before
 * compression.  Free with grk_object_unref() when done.
 *
 * @return pointer to newly allocated @ref grk_image_meta, or NULL on OOM
 */
GRK_API grk_image_meta* GRK_CALLCONV grk_image_meta_new(void);

/**
 * @brief Creates and initializes a JPEG 2000 decompressor.
 *
 * Opens the source stream and reads enough of the header to determine the
 * codec type (J2K codestream or JP2 file-format).  The returned object holds
 * all decompressor state and must be released with grk_object_unref() when
 * the caller is done with it.
 *
 * This function does NOT read the full image header; call
 * grk_decompress_read_header() next to populate @ref grk_header_info.
 *
 * @param stream_params  input stream description (file path, buffer, or
 *                       callbacks — see @ref grk_stream_params)
 * @param params         decompression options (tile range, window region,
 *                       async mode, etc. — see @ref grk_decompress_parameters)
 * @return pointer to an opaque @ref grk_object on success, NULL on failure
 *         (bad stream, unsupported format, OOM)
 */
GRK_API grk_object* GRK_CALLCONV grk_decompress_init(grk_stream_params* stream_params,
                                                     grk_decompress_parameters* params);

/**
 * @brief Updates decompression parameters on an already-initialized codec.
 *
 * Allows changing parameters (e.g. decoding region, reduce factor,
 * quality layers) after grk_decompress_init() but before the first
 * grk_decompress() call.  Not all parameters can be changed mid-stream;
 * refer to @ref grk_decompress_parameters for which fields are live-updatable.
 *
 * @param params updated decompression parameters (see @ref grk_decompress_parameters)
 * @param codec  decompression codec to update (see @ref grk_object)
 * @return true if parameters were applied successfully, false otherwise
 */
GRK_API bool GRK_CALLCONV grk_decompress_update(grk_decompress_parameters* params,
                                                grk_object* codec);

/**
 * @brief Retrieves the current progression state for a cached tile.
 *
 * A progression state describes how many quality layers have been decoded
 * for each resolution level in a given tile.  This is used to implement
 * incremental / partial-quality decompression: decode a tile at reduced
 * quality, inspect it, then decode more layers on demand.
 *
 * Returns a zero-initialised struct if the tile has not been decompressed
 * yet or is not in the tile cache.
 *
 * @param codec       decompression codec (see @ref grk_object)
 * @param tile_index  zero-based tile index (row-major within the tile grid)
 * @return current @ref grk_progression_state for the tile; all-zeros if
 *         the tile has not been decoded
 */
GRK_API grk_progression_state GRK_CALLCONV
    grk_decompress_get_progression_state(grk_object* codec, uint16_t tile_index);

/**
 * @brief Applies a new progression state to one or all cached tiles.
 *
 * When @p state.single_tile is true, only the tile at @p state.tile_index
 * is updated.  The maximum number of quality layers to decode for each
 * resolution level is taken from @p state.layers_per_resolution.
 * If the new limit differs from the current one, the tile is marked dirty
 * so that a subsequent grk_decompress() call re-decodes it with the new
 * layer budget.
 *
 * Typical use: decode a full swath quickly with 1 layer, then apply more
 * layers on demand for specific tiles.
 *
 * @param codec  decompression codec (see @ref grk_object)
 * @param state  desired progression state (see @ref grk_progression_state);
 *               state.single_tile must be true (multi-tile update not supported)
 * @return true if the tile was found in the cache and the state was stored;
 *         false if single_tile is false, the tile is not cached, or the
 *         codec has no decompressor
 */
GRK_API bool GRK_CALLCONV grk_decompress_set_progression_state(grk_object* codec,
                                                               grk_progression_state state);

/**
 * @brief Reads and parses the JPEG 2000 image header.
 *
 * Must be called after grk_decompress_init() and before grk_decompress().
 * Populates @p header_info with image dimensions, tile grid, component
 * descriptions, color space, bit-depths, ICC profile pointer, and
 * progression/quality layer counts.
 *
 * For asynchronous decoding: the image dimensions reported here reflect
 * the full unscaled image; the async scheduler uses them when computing
 * tile ranges for grk_decompress_wait().
 *
 * @param codec        decompression codec (see @ref grk_object)
 * @param header_info  caller-allocated struct to receive header data
 *                     (see @ref grk_header_info); may be NULL if the caller
 *                     only needs to prime the codec for grk_decompress()
 * @return true if the codestream main header (and JP2 superbox, if present)
 *         were read successfully; false on I/O or parse error
 */
GRK_API bool GRK_CALLCONV grk_decompress_read_header(grk_object* codec,
                                                     grk_header_info* header_info);

/**
 * @brief Gets decompressed tile image by tile index.
 *
 * Returns per-tile decoded data after decompression completes.
 * Works for both single-tile and multi-tile images.
 * The tile index is: tile_y * num_tile_cols + tile_x
 * (use grk_wait_swath output fields to compute).
 *
 * @param	codec		decompression codec (see @ref grk_object)
 * @param	tile_index	tile index (row-major within the image tile grid)
 * @param	wait		if true, block until the specified tile is decompressed
 * @return pointer to @ref grk_image for the tile, or NULL if tile not available
 */
GRK_API grk_image* GRK_CALLCONV grk_decompress_get_tile_image(grk_object* codec,
                                                              uint16_t tile_index, bool wait);

/**
 * @brief Gets the composite decompressed image.
 *
 * Returns the composited output image (all tiles merged).
 * Only valid after full decompression completes (grk_decompress_wait with null swath).
 * Requires skip_allocate_composite=false in decompress parameters for data to be present.
 * For per-tile access, prefer grk_decompress_get_tile_image() instead.
 *
 * @param	codec	decompression codec (see @ref grk_object)
 * @return pointer to @ref grk_image
 */
GRK_API grk_image* GRK_CALLCONV grk_decompress_get_image(grk_object* codec);

/**
 * @brief Starts (or continues) decompression of the JPEG 2000 image.
 *
 * For synchronous decoding (asynchronous = false): blocks until all
 * requested tiles are decompressed.  After return, component data is
 * available via grk_decompress_get_image() or grk_decompress_get_tile_image().
 *
 * For asynchronous decoding (asynchronous = true): returns immediately
 * after scheduling decompression tasks on the Taskflow executor.  Use
 * grk_decompress_wait() to synchronize on individual swath regions, and
 * grk_decompress_schedule_swath_copy() + grk_decompress_wait_swath_copy()
 * to copy decoded data into an output buffer.
 *
 * The @p tile parameter is only used when integrating a hardware-
 * accelerator plugin; pass NULL for CPU-only decoding.
 *
 * @param codec  decompression codec (see @ref grk_object)
 * @param tile   plugin tile data, or NULL for CPU-only decoding
 *               (see @ref grk_plugin_tile)
 * @return true if successful (sync) or if scheduling succeeded (async)
 */
GRK_API bool GRK_CALLCONV grk_decompress(grk_object* codec, grk_plugin_tile* tile);

/**
 * @brief Waits for an asynchronous decompression to complete.
 *
 * If swath is non-null: waits for all tiles covering the swath region,
 * then populates swath->tile_x0/y0/x1/y1 and swath->num_tile_cols with
 * the tile grid indices. Use these with grk_decompress_get_tile_image()
 * to retrieve per-tile data.
 *
 * If swath is null: waits for the entire decompression to complete
 * (all tiles, all post-processing). No tile coordinates are output.
 *
 * @param codec codec @ref grk_object
 * @param swath @ref grk_wait_swath to wait for, or NULL for full wait
 */
GRK_API void GRK_CALLCONV grk_decompress_wait(grk_object* codec, grk_wait_swath* swath);

/**
 * @brief Schedule tile-to-swath copies for a completed swath.
 *
 * Call after grk_decompress_wait() returns for the given swath. For each tile
 * in the swath, a Taskflow task is submitted to the library executor that
 * converts the internal int32_t planar tile data into the user-supplied output
 * buffer using Highway SIMD (clamp + right-shift to target precision).
 *
 * Tiles whose decompression Taskflow future is still in flight (ahead of the
 * current swath due to parallel scheduling) have their copy task chained as a
 * Taskflow continuation, so the copy runs as soon as the tile is ready.
 *
 * The output buffer (@ref grk_swath_buffer) uses BSQ layout (one plane per
 * component). Call grk_decompress_wait_swath_copy() to wait for all scheduled
 * copies before accessing buf->data.
 *
 * @param codec codec @ref grk_object
 * @param swath swath descriptor with tile_x0/y0/x1/y1 populated by grk_decompress_wait
 * @param buf   user-managed output buffer; must stay valid until
 *              grk_decompress_wait_swath_copy() returns
 */
GRK_API void GRK_CALLCONV grk_decompress_schedule_swath_copy(grk_object* codec,
                                                             const grk_wait_swath* swath,
                                                             grk_swath_buffer* buf);

/**
 * @brief Wait for all in-flight swath copy tasks to complete.
 *
 * Blocks until every copy task submitted by grk_decompress_schedule_swath_copy()
 * has finished. After this returns, buf->data is fully populated and safe to access.
 *
 * @param codec codec @ref grk_object
 */
GRK_API void GRK_CALLCONV grk_decompress_wait_swath_copy(grk_object* codec);

/**
 * @brief Copy a single decoded tile image into a swath buffer using Highway SIMD.
 *
 * A low-level, synchronous alternative to grk_decompress_schedule_swath_copy()
 * for callers that manage their own threading.  Copies the source component
 * data from @p tile_img into @p buf, clipping to buf's x/y window, applying
 * band_map re-ordering, alpha promotion, and type conversion (int32 → prec/sgnd).
 *
 * Thread-safe: multiple tiles may be copied into the same buf concurrently
 * provided they write to non-overlapping output regions.
 *
 * @param tile_img  tile image returned by grk_decompress_get_tile_image()
 * @param buf       output swath buffer (must be pre-allocated and fully populated)
 */
GRK_API void GRK_CALLCONV grk_copy_tile_to_swath(const grk_image* tile_img,
                                                 const grk_swath_buffer* buf);

/**
 * @brief Decompresses a single tile by index.
 *
 * Decodes only the tile at @p tile_index without decompressing the entire
 * image.  Useful for random-access workflows where only a subset of tiles
 * is needed.  The tile index is row-major: tile_y * num_tile_cols + tile_x.
 *
 * After this returns, retrieve the decoded image via
 * grk_decompress_get_tile_image(codec, tile_index, false).
 *
 * @param codec       decompression codec (see @ref grk_object)
 * @param tile_index  zero-based tile index (row-major within the tile grid)
 * @return true if the tile was decoded successfully, false on error
 */
GRK_API bool GRK_CALLCONV grk_decompress_tile(grk_object* codec, uint16_t tile_index);

/**
 * @brief Gets the number of samples (frames) in the codec container.
 * For single-image formats (JP2, J2K) this returns 1.
 * For multi-frame formats (MJ2) this returns the number of video samples.
 * @param	codec	decompression codec (see @ref grk_object)
 * @return number of samples
 */
GRK_API uint32_t GRK_CALLCONV grk_decompress_num_samples(grk_object* codec);

/**
 * @brief Decompresses a single sample (frame) by index.
 * @param	codec			decompression codec (see @ref grk_object)
 * @param	sample_index	sample index (0-based)
 * @return true if successful, otherwise false
 */
GRK_API bool GRK_CALLCONV grk_decompress_sample(grk_object* codec, uint32_t sample_index);

/**
 * @brief Gets the decompressed image for a specific sample (frame).
 * @param	codec			decompression codec (see @ref grk_object)
 * @param	sample_index	sample index (0-based)
 * @return pointer to @ref grk_image, or NULL if sample not decompressed
 */
GRK_API grk_image* GRK_CALLCONV grk_decompress_get_sample_image(grk_object* codec,
                                                                uint32_t sample_index);

/**
 * @brief Gets a decompressed tile image from a specific sample (frame).
 * Useful for multi-frame containers (MJ2) where each frame may have multiple tiles.
 * The sample must have been decompressed first via grk_decompress() (for sample 0)
 * or grk_decompress_sample() (for subsequent samples).
 * @param	codec			decompression codec (see @ref grk_object)
 * @param	sample_index	sample index (0-based)
 * @param	tile_index		tile index within the sample (row-major)
 * @return pointer to @ref grk_image for the tile, or NULL if not available
 */
GRK_API grk_image* GRK_CALLCONV grk_decompress_get_sample_tile_image(grk_object* codec,
                                                                     uint32_t sample_index,
                                                                     uint16_t tile_index);

/* COMPRESSION FUNCTIONS*/

/**
 * @struct grk_cparameters
 * @brief Compression parameters
 */
typedef struct _grk_cparameters
{
  bool tile_size_on; /* tile size on */
  uint32_t tx0; /* XTOsiz */
  uint32_t ty0; /* YTOsiz */
  uint32_t t_width; /* XTsiz */
  uint32_t t_height; /* YTsiz */
  uint16_t numlayers; /* number of layers */
  /** rate control allocation by rate/distortion curve */
  bool allocation_by_rate_distortion; /* allocation by rate distortion */
  /** layers rates expressed as compression ratios.
   *  They might be subsequently limited by the max_cs_size field */
  double layer_rate[GRK_MAX_LAYERS]; /* layer rate */
  bool allocation_by_quality; /* rate control allocation by fixed_PSNR quality */
  double layer_distortion[GRK_MAX_LAYERS]; /* layer PSNR values */
  char* comment[GRK_NUM_COMMENTS_SUPPORTED]; /* comment */
  uint16_t comment_len[GRK_NUM_COMMENTS_SUPPORTED]; /* comment length */
  bool is_binary_comment[GRK_NUM_COMMENTS_SUPPORTED]; /* is binary comment */
  size_t num_comments; /* number of comments */
  uint8_t csty; /* coding style */
  uint8_t numgbits; /* number of guard bits */
  GRK_PROG_ORDER prog_order; /* progression order (default LRCP)*/
  grk_progression progression[GRK_MAXRLVLS]; /* progression array */
  uint32_t numpocs; /* number of progression order changes (POCs) */
  uint8_t numresolution; /* number of resolutions */
  uint32_t cblockw_init; /* nominal code block width (default 64) */
  uint32_t cblockh_init; /* nominal code block height (default 64) */
  uint8_t cblk_sty; /* code block style */
  bool irreversible; /* true if irreversible compression enabled, default false */
  /** region of interest: affected component in [0..3];
   *  -1 indicates no ROI */
  int32_t roi_compno; /* ROI component number */
  uint32_t roi_shift; /* ROI upshift */
  /* number of precinct size specifications */
  uint32_t res_spec; /* res spec */
  uint32_t prcw_init[GRK_MAXRLVLS]; /* nominal precinct width */
  uint32_t prch_init[GRK_MAXRLVLS]; /* nominal precinct height */
  char infile[GRK_PATH_LEN]; /* input file */
  char outfile[GRK_PATH_LEN]; /* output file */
  uint32_t image_offset_x0; /* image offset x0 */
  uint32_t image_offset_y0; /* image offset y0 */
  uint8_t subsampling_dx; /* subsampling dx */
  uint8_t subsampling_dy; /* subsampling dy */
  GRK_SUPPORTED_FILE_FMT decod_format; /* input decode format */
  GRK_SUPPORTED_FILE_FMT cod_format; /* output code format */
  grk_raw_cparameters raw_cp; /* raw parameters */
  bool enable_tile_part_generation; /* enable tile part generation */
  uint8_t new_tile_part_progression_divider; /* new tile part progression divider */
  uint8_t mct; /* MCT */
  /** Naive implementation of MCT restricted to a single reversible array based
 compressing without offset concerning all the components. */
  void* mct_data; /* MCT data */
  /**
   * Maximum size (in bytes) for the whole code stream.
   * If equal to zero, code stream size limitation is not considered
   * If it does not comply with layer_rate, max_cs_size prevails
   * and a warning is issued.
   * */
  uint64_t max_cs_size; /* max code stream size */
  /**
   * Maximum size (in bytes) for each component.
   * If == 0, component size limitation is not considered
   * */
  uint64_t max_comp_size; /* max component size */
  /** RSIZ value
 To be used to combine GRK_PROFILE_*, GRK_EXTENSION_* and (sub)levels values. */
  uint16_t rsiz; /* RSIZ */
  uint16_t framerate; /* frame rate */

  bool write_capture_resolution_from_file; /* write capture resolution from file */
  double capture_resolution_from_file[2]; /* capture resolution from file */
  bool write_capture_resolution; /* write capture resolution */
  double capture_resolution[2]; /* capture resolution */
  bool write_display_resolution; /* write display resolution */
  double display_resolution[2]; /* display resolution */

  bool apply_icc; /* apply ICC */

  GRK_RATE_CONTROL_ALGORITHM rate_control_algorithm; /* rate control algorithm */
  uint32_t num_threads; /* number of threads */
  int32_t device_id; /* device ID */
  uint32_t duration; /* duration seconds */
  uint32_t kernel_build_options; /* kernel build options */
  uint32_t repeats; /* repeats */
  bool write_plt; /* write PLT */
  bool write_tlm; /* write TLM */
  bool shared_memory_interface; /* shared memory interface */
} grk_cparameters;

/**
 * @brief Fills a @ref grk_cparameters struct with safe default values.
 *
 * Defaults applied:
 * - No rate constraints (lossless)
 * - Tile size: entire image (single tile)
 * - Codeblock size: 64×64
 * - Precinct size: 2^15×2^15 (effectively no sub-precincts)
 * - Number of DWT resolutions: 6
 * - 1 quality layer
 * - No SOP / EPH markers
 * - No ROI upshift
 * - No mode switches (no LAZY, RESET, etc.)
 * - Progression order: LRCP
 * - Reversible 5-3 DWT
 * - Multi-component transform enabled (when 3+ components present)
 * - Image origin (0,0); tile origin (0,0); no POC
 * - Sub-sampling: dx=1, dy=1
 *
 * @param parameters compression parameter block to initialise
 *                   (see @ref grk_cparameters); must not be NULL
 */
GRK_API void GRK_CALLCONV grk_compress_set_default_params(grk_cparameters* parameters);

/**
 * @brief Creates and initializes a JPEG 2000 compressor.
 *
 * Allocates and prepares all compression state for the provided image.
 * The output destination (file, buffer, or callbacks) is described by
 * @p stream_params. Encoding parameters (tile size, DWT levels, quality
 * layers, etc.) are taken from @p parameters — call
 * grk_compress_set_default_params() first to populate defaults.
 *
 * The @p image must remain valid until grk_compress() completes. The
 * returned codec object must be released with grk_object_unref().
 *
 * @param stream_params  output stream description (see @ref grk_stream_params)
 * @param parameters     compression settings (see @ref grk_cparameters)
 * @param image          source image to compress (see @ref grk_image)
 * @return pointer to an opaque @ref grk_object on success, NULL on failure
 *         (invalid parameters, unsupported image, OOM)
 */
GRK_API grk_object* GRK_CALLCONV grk_compress_init(grk_stream_params* stream_params,
                                                   grk_cparameters* parameters, grk_image* image);
/**
 * @brief Compresses the image into a JPEG 2000 codestream.
 *
 * Performs the full encode pipeline (MCT, DWT, T1, rate control, T2)
 * and writes the result to the output stream supplied to grk_compress_init().
 *
 * The @p tile parameter is only used when a hardware-accelerator plugin
 * performs some encoding stages; pass NULL for CPU-only encoding.
 *
 * @param codec  compression codec (see @ref grk_object)
 * @param tile   plugin tile data, or NULL for CPU-only encoding
 *               (see @ref grk_plugin_tile)
 * @return number of bytes written to the output stream, or 0 on failure
 */
GRK_API uint64_t GRK_CALLCONV grk_compress(grk_object* codec, grk_plugin_tile* tile);

/**
 * @brief Compresses an additional frame into a multi-frame container (MJ2).
 * For single-image formats (JP2, J2K) this returns 0 (unsupported).
 * @param codec compression codec (see @ref grk_object)
 * @param image Input image for this frame (see @ref grk_image)
 * @param tile	plugin tile (see @ref grk_plugin_tile)
 * @return number of bytes written if successful, 0 otherwise
 */
GRK_API uint64_t GRK_CALLCONV grk_compress_frame(grk_object* codec, grk_image* image,
                                                 grk_plugin_tile* tile);

/**
 * @brief Finalizes a multi-frame compress container (e.g. writes MJ2 moov box).
 * For single-image formats this is a no-op.
 * @param codec compression codec (see @ref grk_object)
 * @return true if successful, otherwise false
 */
GRK_API bool GRK_CALLCONV grk_compress_finish(grk_object* codec);

/**
 * @brief Gets the total compressed length (bytes written to output stream/buffer)
 * @param	codec	compression codec (see @ref grk_object)
 * @return	compressed length in bytes, or 0 on failure
 */
GRK_API uint64_t GRK_CALLCONV grk_compress_get_compressed_length(grk_object* codec);

/**
 * @brief Dumps codec diagnostic information to a stream.
 *
 * Writes human-readable codec state (image dimensions, tile grid,
 * progression order, codestream indices, etc.) to @p output_stream.
 * Useful for debugging and conformance testing.
 *
 * The @p info_flag controls which sections are emitted; combine the
 * GRK_IMG_INFO, GRK_MH_INFO, GRK_TH_INFO, GRK_TCH_INFO, GRK_MH_IND,
 * and GRK_TH_IND flags as needed.
 *
 * @param codec          codec (decompression or compression — see @ref grk_object)
 * @param info_flag      bitmask of GRK_*_INFO / GRK_*_IND flags
 * @param output_stream  destination FILE* (e.g. stdout, stderr, or a log file)
 */
GRK_API void GRK_CALLCONV grk_dump_codec(grk_object* codec, uint32_t info_flag,
                                         FILE* output_stream);

/**
 * @brief Installs a custom Multi-Component Transform (MCT) matrix.
 *
 * Overrides the default RGB→YCbCr colour transform with a user-supplied
 * floating-point matrix.  The matrix must be square (@p nb_comp × @p nb_comp)
 * stored in row-major order.  A matching DC shift vector (one value per
 * component) is applied after the transform.
 *
 * Only meaningful for compression; ignored for decompression.
 * Must be called before grk_compress_init().
 *
 * @param parameters       compression parameters to update (see @ref grk_cparameters)
 * @param encoding_matrix  row-major nb_comp×nb_comp transform matrix
 * @param dc_shift         per-component DC shift values (array of @p nb_comp int32_t)
 * @param nb_comp          number of image components (matrix dimension)
 * @return true if the matrix was stored successfully, false on invalid input
 */
GRK_API bool GRK_CALLCONV grk_set_MCT(grk_cparameters* parameters, const float* encoding_matrix,
                                      const int32_t* dc_shift, uint32_t nb_comp);

/* Decoder state flags */
#define GRK_IMG_INFO 1 /* Basic image information provided to the user */
#define GRK_MH_INFO 2 /* Codestream information based only on the main header */
#define GRK_TH_INFO 4 /* Tile information based on the current tile header */
#define GRK_TCH_INFO 8 /** Tile/Component information of all tiles */
#define GRK_MH_IND 16 /** Codestream index based only on the main header */
#define GRK_TH_IND 32 /** Tile index based on the current tile */

/* Code block styles */
#define GRK_CBLKSTY_LAZY 0x01 /** Selective arithmetic coding bypass */
#define GRK_CBLKSTY_RESET 0x02 /** Reset context probabilities on coding pass boundaries */
#define GRK_CBLKSTY_TERMALL 0x04 /** Termination on each coding pass */
#define GRK_CBLKSTY_VSC 0x08 /** Vertical stripe causal context */
#define GRK_CBLKSTY_PTERM 0x10 /** Predictable termination */
#define GRK_CBLKSTY_SEGSYM 0x20 /** Segmentation symbols are used */
#define GRK_CBLKSTY_HT_ONLY 0x40 /** high throughput only block coding */
#define GRK_CBLKSTY_HT_MIXED 0xC0 /** mixed high throughput block coding */
#define GRK_JPH_RSIZ_FLAG 0x4000 /**for JPH, bit 14 of RSIZ must be set to 1 */

/*****************************************************************************
 * JPEG 2000 Profiles, see Table A.10 from 15444-1 (updated in various AMDs)
 *
 * These values help choose the RSIZ value for the JPEG 2000 code stream.
 * The RSIZ value forces various compressing options, as detailed in Table A.10.
 * If GRK_PROFILE_PART2 is chosen, it must be combined with one or more extensions
 * described below.
 *
 *   Example: rsiz = GRK_PROFILE_PART2 | GRK_EXTENSION_MCT;
 *
 * For broadcast profiles, the GRK_PROFILE_X value has to be combined with the target
 * level (3-0 LSB, value between 0 and 11):
 *   Example: rsiz = GRK_PROFILE_BC_MULTI | 0x0005; (level equals 5)
 *
 * For IMF profiles, the GRK_PROFILE_X value has to be combined with the target main-level
 * (3-0 LSB, value between 0 and 11) and sub-level (7-4 LSB, value between 0 and 9):
 *   Example: rsiz = GRK_PROFILE_IMF_2K | 0x0040 | 0x0005; (main-level equals 5 and sub-level
 * equals 4
 *
 * */
#define GRK_PROFILE_NONE 0x0000 /** no profile, conform to 15444-1 */
#define GRK_PROFILE_0 0x0001 /** Profile 0 as described in 15444-1,Table A.45 */
#define GRK_PROFILE_1 0x0002 /** Profile 1 as described in 15444-1,Table A.45 */
#define GRK_PROFILE_CINEMA_2K 0x0003 /** 2K cinema profile defined in 15444-1 AMD1 */
#define GRK_PROFILE_CINEMA_4K 0x0004 /** 4K cinema profile defined in 15444-1 AMD1 */
#define GRK_PROFILE_CINEMA_S2K 0x0005 /** Scalable 2K cinema profile defined in 15444-1 AMD2 */
#define GRK_PROFILE_CINEMA_S4K 0x0006 /** Scalable 4K cinema profile defined in 15444-1 AMD2 */
#define GRK_PROFILE_CINEMA_LTS \
  0x0007 /** Long term storage cinema profile defined in 15444-1 AMD2 */
#define GRK_PROFILE_BC_SINGLE 0x0100 /** Single Tile Broadcast profile defined in 15444-1 AMD3 */
#define GRK_PROFILE_BC_MULTI 0x0200 /** Multi Tile Broadcast profile defined in 15444-1 AMD3 */
#define GRK_PROFILE_BC_MULTI_R \
  0x0300 /** Multi Tile Reversible Broadcast profile defined in 15444-1 AMD3 */
#define GRK_PROFILE_BC_MASK 0x030F /** Mask for broadcast profile including main level */
#define GRK_PROFILE_IMF_2K 0x0400 /** 2K Single Tile Lossy IMF profile defined in 15444-1 AMD8 */
#define GRK_PROFILE_IMF_4K 0x0500 /** 4K Single Tile Lossy IMF profile defined in 15444-1 AMD8 */
#define GRK_PROFILE_IMF_8K 0x0600 /** 8K Single Tile Lossy IMF profile defined in 15444-1 AMD8 */
#define GRK_PROFILE_IMF_2K_R \
  0x0700 /** 2K Single/Multi Tile Reversible IMF profile defined in 15444-1 AMD8 */
#define GRK_PROFILE_IMF_4K_R \
  0x0800 /** 4K Single/Multi Tile Reversible IMF profile defined in 15444-1 AMD8 */
#define GRK_PROFILE_IMF_8K_R \
  0x0900 /** 8K Single/Multi Tile Reversible IMF profile defined in 15444-1 AMD8 */
#define GRK_PROFILE_MASK 0x0FFF /** Mask for profile bits */
#define GRK_PROFILE_PART2 0x8000 /** At least 1 extension defined in 15444-2 (Part-2) */
#define GRK_PROFILE_PART2_EXTENSIONS_MASK 0x3FFF /*  Mask for Part-2 extension bits */

/**
 * JPEG 2000 Part-2 extensions
 * */
#define GRK_EXTENSION_NONE 0x0000 /** No Part-2 extension */
#define GRK_EXTENSION_MCT 0x0100 /** Custom MCT support */
#define GRK_IS_PART2(v) ((v) & GRK_PROFILE_PART2)

#define GRK_IS_CINEMA(v) (((v) >= GRK_PROFILE_CINEMA_2K) && ((v) <= GRK_PROFILE_CINEMA_S4K))
#define GRK_IS_STORAGE(v) ((v) == GRK_PROFILE_CINEMA_LTS)

/*
 *
 * *********************************************
 * Broadcast level (3-0 LSB) (15444-1 AMD4,AMD8)
 * *********************************************
 *
 * indicates maximum bit rate and sample rate for a code stream
 *
 * Note: Mbit/s == 10^6 bits/s;  Msamples/s == 10^6 samples/s
 *
 * 0:       no maximum rate
 * 1:       200 Mbits/s, 65  Msamples/s
 * 2:       200 Mbits/s, 130 Msamples/s
 * 3:       200 Mbits/s, 195 Msamples/s
 * 4:       400 Mbits/s, 260 Msamples/s
 * 5:       800Mbits/s,  520 Msamples/s
 * >= 6:    2^(level-6) * 1600 Mbits/s, 2^(level-6) * 1200 Msamples/s
 *
 * Note: level cannot be greater than 11
 *
 * ****************
 * Broadcast tiling
 * ****************
 *
 * Either single-tile or multi-tile. Multi-tile only permits
 * 1 or 4 tiles per frame, where multiple tiles have identical
 * sizes, and are configured in either 2x2 or 1x4 layout.
 *
 *************************************************************
 *
 * ***************************************
 * IMF main-level (3-0) LSB (15444-1 AMD8)
 * ***************************************
 *
 * main-level indicates maximum number of samples per second,
 * as listed above.
 *
 *
 * **************************************
 * IMF sub-level (7-4) LSB (15444-1 AMD8)
 * **************************************
 *
 * sub-level indicates maximum bit rate for a code stream:
 *
 * 0:   no maximum rate
 * >0:  2^sub-level * 100 Mbits/second
 *
 * Note: sub-level cannot be greater than 9, and cannot be larger
 * then maximum of (main-level -2) and 1.
 *
 */

#define GRK_GET_IMF_OR_BROADCAST_PROFILE(v) \
  ((v) & 0x0f00) /** Extract profile without mainlevel/sublevel */

#define GRK_LEVEL_MAX 11U /** Maximum (main) level */
#define GRK_GET_LEVEL(v) ((v) & 0xf) /** Extract (main) level */

/******* BROADCAST **********************************************************/

#define GRK_IS_BROADCAST(v)                                                        \
  (((v) >= GRK_PROFILE_BC_SINGLE) && ((v) <= (GRK_PROFILE_BC_MULTI_R | 0x000b)) && \
   (((v) & 0xf) <= 0xb))

/* Maximum component sampling Rate (Mbits/sec) per level */
#define GRK_BROADCAST_LEVEL_1_MBITSSEC 200U /** Mbits/sec for level 1 */
#define GRK_BROADCAST_LEVEL_2_MBITSSEC 200U /** Mbits/sec for level 2 */
#define GRK_BROADCAST_LEVEL_3_MBITSSEC 200U /** Mbits/sec for level 3 */
#define GRK_BROADCAST_LEVEL_4_MBITSSEC 400U /** Mbits/sec for level 4 */
#define GRK_BROADCAST_LEVEL_5_MBITSSEC 800U /** Mbits/sec for level 5 */
#define GRK_BROADCAST_LEVEL_6_MBITSSEC 1600U /** Mbits/sec for level 6 */
#define GRK_BROADCAST_LEVEL_7_MBITSSEC 3200U /** Mbits/sec for level 7 */
#define GRK_BROADCAST_LEVEL_8_MBITSSEC 6400U /** Mbits/sec for level 8 */
#define GRK_BROADCAST_LEVEL_9_MBITSSEC 12800U /** Mbits/sec for level 9 */
#define GRK_BROADCAST_LEVEL_10_MBITSSEC 25600U /** Mbits/sec for level 10 */
#define GRK_BROADCAST_LEVEL_11_MBITSSEC 51200U /** Mbits/sec for level 11 */

#define GRK_BROADCAST_LEVEL_1_MSAMPLESSEC 64U /** MSamples/sec for level 1 */
#define GRK_BROADCAST_LEVEL_2_MSAMPLESSEC 130U /** MSamples/sec for level 2 */
#define GRK_BROADCAST_LEVEL_3_MSAMPLESSEC 195U /** MSamples/sec for level 3 */
#define GRK_BROADCAST_LEVEL_4_MSAMPLESSEC 260U /** MSamples/sec for level 4 */
#define GRK_BROADCAST_LEVEL_5_MSAMPLESSEC 520U /** MSamples/sec for level 5 */
#define GRK_BROADCAST_LEVEL_6_MSAMPLESSEC 1200U /** MSamples/sec for level 6 */
#define GRK_BROADCAST_LEVEL_7_MSAMPLESSEC 2400U /** MSamples/sec for level 7 */
#define GRK_BROADCAST_LEVEL_8_MSAMPLESSEC 4800U /** MSamples/sec for level 8 */
#define GRK_BROADCAST_LEVEL_9_MSAMPLESSEC 9600U /** MSamples/sec for level 9 */
#define GRK_BROADCAST_LEVEL_10_MSAMPLESSEC 19200U /** MSamples/sec for level 10 */
#define GRK_BROADCAST_LEVEL_11_MSAMPLESSEC 38400U /** MSamples/sec for level 11 */

/********IMF *****************************************************************/

#define GRK_IS_IMF(v)                                                         \
  (((v) >= GRK_PROFILE_IMF_2K) && ((v) <= (GRK_PROFILE_IMF_8K_R | 0x009b)) && \
   (((v) & 0xf) <= 0xb) && (((v) & 0xf0) <= 0x90))

/* Maximum component sampling rate (MSamples/sec) per main level */
#define GRK_IMF_MAINLEVEL_1_MSAMPLESSEC 65U /** MSamples/sec for main level 1 */
#define GRK_IMF_MAINLEVEL_2_MSAMPLESSEC 130U /** MSamples/sec for main level 2 */
#define GRK_IMF_MAINLEVEL_3_MSAMPLESSEC 195U /** MSamples/sec for main level 3 */
#define GRK_IMF_MAINLEVEL_4_MSAMPLESSEC 260U /** MSamples/sec for main level 4 */
#define GRK_IMF_MAINLEVEL_5_MSAMPLESSEC 520U /** MSamples/sec for main level 5 */
#define GRK_IMF_MAINLEVEL_6_MSAMPLESSEC 1200U /** MSamples/sec for main level 6 */
#define GRK_IMF_MAINLEVEL_7_MSAMPLESSEC 2400U /** MSamples/sec for main level 7 */
#define GRK_IMF_MAINLEVEL_8_MSAMPLESSEC 4800U /** MSamples/sec for main level 8 */
#define GRK_IMF_MAINLEVEL_9_MSAMPLESSEC 9600U /** MSamples/sec for main level 9 */
#define GRK_IMF_MAINLEVEL_10_MSAMPLESSEC 19200U /** MSamples/sec for main level 10 */
#define GRK_IMF_MAINLEVEL_11_MSAMPLESSEC 38400U /** MSamples/sec for main level 11 */

#define GRK_IMF_SUBLEVEL_MAX 9U /** Maximum IMF sublevel */
#define GRK_GET_IMF_SUBLEVEL(v) (((v) >> 4) & 0xf) /** Extract IMF sub level */

/** Maximum compressed bit rate (Mbits/s) per IMF sub level */
#define GRK_IMF_SUBLEVEL_1_MBITSSEC 200U /** Mbits/s for IMF sub level 1 */
#define GRK_IMF_SUBLEVEL_2_MBITSSEC 400U /** Mbits/s for IMF sub level 2 */
#define GRK_IMF_SUBLEVEL_3_MBITSSEC 800U /** Mbits/s for IMF sub level 3 */
#define GRK_IMF_SUBLEVEL_4_MBITSSEC 1600U /** Mbits/s for IMF sub level 4 */
#define GRK_IMF_SUBLEVEL_5_MBITSSEC 3200U /** Mbits/s for IMF sub level 5 */
#define GRK_IMF_SUBLEVEL_6_MBITSSEC 6400U /** Mbits/s for IMF sub level 6 */
#define GRK_IMF_SUBLEVEL_7_MBITSSEC 12800U /** Mbits/s for IMF sub level 7 */
#define GRK_IMF_SUBLEVEL_8_MBITSSEC 25600U /** Mbits/s for IMF sub level 8 */
#define GRK_IMF_SUBLEVEL_9_MBITSSEC 51200U /** Mbits/s for IMF sub level 9 */
/**********************************************************************************/

/**
 * JPEG 2000 cinema profile code stream and component size limits
 * */

#define GRK_CINEMA_DCI_MAX_BANDWIDTH 250000000

#define GRK_CINEMA_24_CS 1302083 /** Maximum code stream length @ 24fps */
#define GRK_CINEMA_24_COMP 1041666 /** Maximum size per color component @ 24fps */

#define GRK_CINEMA_48_CS 651041 /** Maximum code stream length @ 48fps */
#define GRK_CINEMA_48_COMP 520833 /** Maximum size per color component @ 48fps */

#define GRK_CINEMA_4K_DEFAULT_NUM_RESOLUTIONS 7 /* Default number of resolutions for 4K cinema */

/*
 * CIE Lab #defines
 */
#define GRK_CUSTOM_CIELAB_SPACE 0x0
#define GRK_DEFAULT_CIELAB_SPACE 0x44454600 /* 'DEF' */

/*************************************************************************************
 Plugin Interface
 *************************************************************************************/

/**
 * @struct grk_plugin_load_info
 * @brief Plugin load info
 *
 */
typedef struct _grk_plugin_load_info
{
  const char* pluginPath; /* plugin path */
} grk_plugin_load_info;

/**
 * @brief Loads a hardware-accelerator plugin (.so / .dll).
 *
 * Should be called after grk_initialize(), before grk_compress_init() or
 * grk_decompress_init().  The plugin extends Grok with GPU or FPGA
 * acceleration for T1 entropy coding.  If loading fails, Grok falls back
 * to CPU-only operation transparently.
 *
 * Only one plugin may be loaded at a time.  Call grk_plugin_cleanup() to
 * unload the current plugin before loading a new one.
 *
 * @param info  plugin path and options (see @ref grk_plugin_load_info)
 * @return true if the plugin was loaded and its ABI version is compatible,
 *         false otherwise
 */
GRK_API bool GRK_CALLCONV grk_plugin_load(grk_plugin_load_info info);

/**
 * @brief Unloads the plugin and releases all plugin-owned resources.
 *
 * Signals the plugin to drain any in-flight GPU/FPGA work, then closes
 * the shared library handle.  Call before process exit or before loading
 * a different plugin version.
 *
 * Safe to call even if no plugin is loaded (no-op in that case).
 */
GRK_API void GRK_CALLCONV grk_plugin_cleanup(void);

/**
 * @brief No debug is done on plugin. Production setting.
 */
#define GRK_PLUGIN_STATE_NO_DEBUG 0x0

/*
 For compress debugging, the plugin first performs a T1 compress.
 Then:
 1. perform host DWT on plugin MCT data, and write to host image
 This way, both plugin and host start from same point
 (assume MCT is equivalent for both host and plugin)
 2. map plugin DWT data, compare with host DWT, and then write to plugin image
 At this point in the code, the plugin image holds plugin DWT data. And if no warnings are
 triggered, then we can safely say that host and plugin DWT data are identical.
 3. Perform host compress, skipping MCT and DWT (they have already been performed)
 4. during host compress, each context that is formed is compared against context stream from plugin
 5. rate control - synch with plugin code stream, and compare
 6. T2 and store to disk
 */

#define GRK_PLUGIN_STATE_DEBUG 0x1
#define GRK_PLUGIN_STATE_PRE_TR1 0x2
#define GRK_PLUGIN_STATE_DWT_QUANTIZATION 0x4
#define GRK_PLUGIN_STATE_MCT_ONLY 0x8

/**
 * @brief Returns the current debug state bitmask of the loaded plugin.
 *
 * The returned value is a combination of GRK_PLUGIN_STATE_* flags:
 * - GRK_PLUGIN_STATE_NO_DEBUG (0x0)         — production mode, no debug output
 * - GRK_PLUGIN_STATE_DEBUG (0x1)            — T1 debug comparisons enabled
 * - GRK_PLUGIN_STATE_PRE_TR1 (0x2)          — pre-T1 DWT/MCT data compared
 * - GRK_PLUGIN_STATE_DWT_QUANTIZATION (0x4) — DWT quantisation compared
 * - GRK_PLUGIN_STATE_MCT_ONLY (0x8)         — only MCT stage compared
 *
 * Returns 0 (GRK_PLUGIN_STATE_NO_DEBUG) if no plugin is loaded.
 *
 * @return bitmask of active GRK_PLUGIN_STATE_* flags
 */
GRK_API uint32_t GRK_CALLCONV grk_plugin_get_debug_state();

/*
 * @struct grk_plugin_init_info
 * @brief Plugin init info
 */
typedef struct _grk_plugin_init_info
{
  int32_t device_id; /* device ID */
  const char* license; /* license */
  const char* server; /* server */
} grk_plugin_init_info;

/**
 * @brief Initializes a loaded plugin with a device and license.
 *
 * Must be called after grk_plugin_load() and before any compress or
 * decompress operation that uses the plugin.  Connects the plugin to
 * the specified accelerator device and validates the license string.
 *
 * @param init_info  device ID, license key, and optional server address
 *                   (see @ref grk_plugin_init_info)
 * @return true if the plugin accepted the device and license,
 *         false on invalid license, device unavailable, or no plugin loaded
 */
GRK_API bool GRK_CALLCONV grk_plugin_init(grk_plugin_init_info init_info);

/**
 * @struct grk_plugin_compress_user_callback_info
 * @brief Plugin compress user callback info
 */
typedef struct grk_plugin_compress_user_callback_info
{
  const char* input_file_name; /* input file name */
  bool output_file_name_is_relative; /* output file name is relative */
  const char* output_file_name; /* output file name */
  grk_cparameters* compressor_parameters; /* compressor parameters */
  grk_image* image; /* image */
  grk_plugin_tile* tile; /* tile */
  grk_stream_params stream_params; /* stream parameters */
  unsigned int error_code; /* error code */
  bool transfer_exif_tags; /* transfer EXIF tags */
} grk_plugin_compress_user_callback_info;

/**
 * @brief Plugin compress user callback
 * @param info callback info (see @ref grk_plugin_compress_user_callback_info)
 */
typedef uint64_t (*GRK_PLUGIN_COMPRESS_USER_CALLBACK)(grk_plugin_compress_user_callback_info* info);

/**
 * @brief Plugin batch compress info
 */
typedef struct grk_plugin_compress_batch_info
{
  const char* input_dir; /* input directory */
  const char* output_dir; /* output directory */
  grk_cparameters* compress_parameters; /* compress parameters */
  GRK_PLUGIN_COMPRESS_USER_CALLBACK callback; /* callback */
} grk_plugin_compress_batch_info;

/**
 * @brief Compresses a single image using the loaded hardware plugin.
 *
 * Uses the plugin-accelerated T1 encoder.  For CPU-only encoding use
 * grk_compress() instead.  The @p callback is invoked on completion
 * with the compressed byte count and any error code.
 *
 * @param compress_parameters  compression settings (see @ref grk_cparameters)
 * @param callback             completion callback
 *                             (see @ref GRK_PLUGIN_COMPRESS_USER_CALLBACK)
 * @return 0 on success, non-zero error code on failure
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_compress(grk_cparameters* compress_parameters,
                                                 GRK_PLUGIN_COMPRESS_USER_CALLBACK callback);

/**
 * @brief Compresses a directory of images in batch using the hardware plugin.
 *
 * Scans @p info.input_dir for supported source images, compresses each one
 * with plugin acceleration, and writes results to @p info.output_dir.
 * A per-image @p info.callback is invoked for each file to allow
 * custom output naming and post-processing.
 *
 * @param info  batch job description: input/output directories, compression
 *              parameters, and per-file callback
 *              (see @ref grk_plugin_compress_batch_info)
 * @return 0 if all files were queued successfully, non-zero on error
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_batch_compress(grk_plugin_compress_batch_info info);

/**
 * @brief Blocks until all pending plugin batch jobs have completed.
 *
 * Should be called after grk_plugin_batch_compress() returns to ensure
 * all asynchronously queued compression tasks have finished before the
 * caller inspects output files or calls grk_plugin_cleanup().
 */
GRK_API void GRK_CALLCONV grk_plugin_wait_for_batch_complete(void);

/**
 * @brief Requests cancellation of a running batch compress operation.
 *
 * Sets a stop flag that causes the batch scheduler to skip remaining
 * queued files.  Already-in-flight jobs are allowed to complete.
 * Call grk_plugin_wait_for_batch_complete() after this to ensure
 * a clean shutdown.
 */
GRK_API void GRK_CALLCONV grk_plugin_stop_batch_compress(void);

/**
 * @brief Plugin init decompressors
 */
typedef int (*GROK_INIT_DECOMPRESSORS)(grk_header_info* header_info, grk_image* image);

/**
 * @struct grk_plugin_decompress_callback_info
 * @brief Plugin decompress callback info
 */
typedef struct _grk_plugin_decompress_callback_info
{
  size_t device_id; /* device ID */
  GROK_INIT_DECOMPRESSORS init_decompressors_func; /* init decompressors func */
  const char* input_file_name; /* input file name */
  const char* output_file_name; /* output file name */
  /* input file format 0: J2K, 1: JP2 */
  GRK_CODEC_FORMAT decod_format; /* decode format */
  /* output file format 0: PGX, 1: PxM, 2: BMP etc */
  GRK_SUPPORTED_FILE_FMT cod_format; /* code format */
  grk_object* codec; /* codec */
  grk_header_info header_info; /* header info */
  grk_decompress_parameters* decompressor_parameters; /* decompressor parameters */
  grk_image* image; /* image */
  bool plugin_owns_image; /* plugin owns image */
  grk_plugin_tile* tile; /* tile */
  unsigned int error_code; /* error code */
  uint32_t decompress_flags; /* decompress flags */
  uint32_t full_image_x0; /* full image x0 */
  uint32_t full_image_y0; /* full image y0 */
  void* user_data; /* user data */
} grk_plugin_decompress_callback_info;

/**
 * @brief Plugin decompress callback
 * @param info callback info (see @ref grk_plugin_decompress_callback_info)
 * @return 0 if successful, otherwise return error code
 */
typedef int32_t (*grk_plugin_decompress_callback)(grk_plugin_decompress_callback_info* info);

/**
 * @brief Decompresses a single JPEG 2000 image using the loaded hardware plugin.
 *
 * Uses plugin-accelerated T1 entropy decoding.  For CPU-only decoding use
 * grk_decompress() instead.  The @p callback is invoked on completion.
 *
 * @param decompress_parameters  decompression settings
 *                               (see @ref grk_decompress_parameters)
 * @param callback               per-image completion callback
 *                               (see @ref grk_plugin_decompress_callback)
 * @return 0 on success, non-zero error code on failure
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_decompress(grk_decompress_parameters* decompress_parameters,
                                                   grk_plugin_decompress_callback callback);

/**
 * @brief Initialises a batch plugin decompress operation but does not start it.
 *
 * Sets up a batch decompression pipeline: scans @p input_dir, pairs each
 * file with @p decompress_parameters, and registers @p callback for
 * per-file results.  Call grk_plugin_batch_decompress() to start decoding.
 *
 * @param input_dir              directory containing JPEG 2000 source files
 * @param output_dir             directory to write decompressed output files
 * @param decompress_parameters  shared decompression settings for all files
 *                               (see @ref grk_decompress_parameters)
 * @param callback               per-image result callback
 *                               (see @ref grk_plugin_decompress_callback)
 * @return 0 on success, non-zero on error (missing directory, bad parameters)
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_init_batch_decompress(
    const char* input_dir, const char* output_dir, grk_decompress_parameters* decompress_parameters,
    grk_plugin_decompress_callback callback);

/**
 * @brief Starts (resumes) the queued batch decompress operation.
 *
 * Begins processing files registered by grk_plugin_init_batch_decompress().
 * Returns immediately; use grk_plugin_wait_for_batch_complete() (or poll
 * via callbacks) to track completion.
 *
 * @return 0 on success, non-zero if the batch was not initialised or
 *         a fatal scheduling error occurred
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_batch_decompress(void);

/**
 * @brief Requests cancellation of a running batch decompress operation.
 *
 * Sets a stop flag that causes the batch scheduler to skip remaining
 * files.  Already-in-flight jobs complete normally.  After calling this,
 * wait for grk_plugin_wait_for_batch_complete() before accessing output.
 */
GRK_API void GRK_CALLCONV grk_plugin_stop_batch_decompress(void);

#ifndef SWIG
#ifdef __cplusplus
}
#endif
#endif
