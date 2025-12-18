/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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
 * @brief If this variable is set (to any value) then codec is
 * forced into single thread mode
 *
 * GRK_TEST_SINGLE
 *
 * @brief If this variable is set to non-zero value, then codec scheduling
 * is set to windowed, otherwise it is set to whole-tile
 *
 * GRK_WINDOWED_SCHEDULING
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
  GRK_FMT_YUV /* YUV */
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
  char username[129]; // AWS access key ID: max 128 chars + null
  char password[129]; // Secret access key: max 128 chars + null
  char bearer_token[4097]; // Session token: up to 4096 chars + null
  char custom_header[1024]; // Single header: fits within 8KB total limit
  char region[64]; // Region code: max 50 chars + buffer

} grk_stream_params;

/**
 * @brief Grok tile cache strategy
 */
#define GRK_TILE_CACHE_NONE 0 /* no tile caching */
#define GRK_TILE_CACHE_IMAGE 1 /* cache final tile image */
#define GRK_TILE_CACHE_ALL 2 /* cache everything */

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
  bool single_tile_decompress; /* single tile decompress */
  grk_precision* precision; /* precision array */
  uint32_t num_precision; /* size of precision array*/
  bool force_rgb; /* force output to sRGB */
  bool upsample; /* upsample components according to their dx and dy values*/
  bool split_pnm; /* split output components to different files for PNM */
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

  /**************************************************************
  Variables below are set by client only if decompressing to file
  **************************************************************/
  GRK_SUPPORTED_FILE_FMT decompress_fmt; /* decompress format */
  bool force_rgb; /* force RGB */
  bool upsample; /* upsample */
  grk_precision* precision; /* precision */
  uint32_t num_precision; /* number of precision */
  bool split_by_component; /* split by component */
  bool single_tile_decompress; /* single tile decompress */
} grk_header_info;

/**
 * @struct grk_wait_swath
 * @brief Specify swath to wait on
 * Holds swath coordinates and tile indices covering swath
 * Tile indices are set by library
 */
typedef struct grk_wait_swath
{
  uint32_t x0; // Pixel coordinate: top-left x
  uint32_t y0; // Pixel coordinate: top-left y
  uint32_t x1; // Pixel coordinate: bottom-right x (exclusive)
  uint32_t y1; // Pixel coordinate: bottom-right y (exclusive)
  uint16_t tile_x0; // Global tile coordinate: start x (inclusive)
  uint16_t tile_y0; // Global tile coordinate: start y (inclusive)
  uint16_t tile_x1; // Global tile coordinate: end x (exclusive)
  uint16_t tile_y1; // Global tile coordinate: end y (exclusive)
  uint16_t num_tile_cols; // Number of tile columns in the image's tile grid
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
 * @brief Gets library version
 */
GRK_API const char* GRK_CALLCONV grk_version(void);

/**
 * @brief Initializes Grok library
 * Must be called before any Grok API calls
 * @param plugin_path 	path to plugin - set this to NULL
 * @param num_threads number of threads to use for compress/decompress - set to 0 to use all threads
 * @param plugin_initialized if plugin is initialized, this pointer to bool will be set to true,
 * otherwise false. Set this to NULL.
 */
GRK_API void GRK_CALLCONV grk_initialize(const char* plugin_path, uint32_t num_threads,
                                         bool* plugin_initialized);

/**
 * @brief Increments ref count
 * @param obj Grok object (see @ref grk_object)
 */
GRK_API grk_object* GRK_CALLCONV grk_object_ref(grk_object* obj);

/*
 * @brief Decrements ref count
 * @param obj Grok object (see @ref grk_object)
 */
GRK_API void GRK_CALLCONV grk_object_unref(grk_object* obj);

/**
 * @brief Sets log message handlers
 * @param msg_handlers see @ref grk_msg_handlers
 */
GRK_API void GRK_CALLCONV grk_set_msg_handlers(grk_msg_handlers msg_handlers);

/**
 * @brief Creates image
 * @param numcmpts      number of components
 * @param cmptparms     component parameters (see @ref grk_image_comp)
 * @param clrspc        image color space (see @ref GRK_COLOR_SPACE)
 * @param alloc_data    if true, allocate component data buffers
 * @return returns      a new image if successful, otherwise NULL
 * */
GRK_API grk_image* GRK_CALLCONV grk_image_new(uint16_t numcmpts, grk_image_comp* cmptparms,
                                              GRK_COLOR_SPACE clrspc, bool alloc_data);

/**
 * @brief Creates meta
 */
GRK_API grk_image_meta* GRK_CALLCONV grk_image_meta_new(void);

/**
 * @brief Initializes decompressor
 * @param stream_params 	source stream parameters (see @ref grk_stream_params)
 * @param params 	decompress parameters (see @ref grk_decompress_parameters)
 * object will be created, otherwise the codec object stored in *codec will be
 * re-initialized
 *
 * @return pointer to @ref grk_object if successful, otherwise nullptr
 */
GRK_API grk_object* GRK_CALLCONV grk_decompress_init(grk_stream_params* stream_params,
                                                     grk_decompress_parameters* params);

/**
 * @brief Updates decompressor
 * @param params 	decompress parameters (see @ref grk_decompress_parameters)
 * @param codec codec (see @ref grk_object)
 * @return true if successful, otherwise false
 */
GRK_API bool GRK_CALLCONV grk_decompress_update(grk_decompress_parameters* params,
                                                grk_object* codec);

/**
 * @brief Gets @ref grk_progression_state for a tile
 * @param codec codec (see @ref grk_object)
 * @param tile_index tile index
 * @return @ref grk_progression_state. Struct will be all zeros if tile has not been decompressed
 * yet
 */
GRK_API grk_progression_state GRK_CALLCONV
    grk_decompress_get_progression_state(grk_object* codec, uint16_t tile_index);

/**
 * @brief Sets @ref grk_progression_state for a tile
 * @param codec codec (see @ref grk_object)
 * @param state @ref grk_progression_state
 * @return true if tile exists in cache and state marked tile as dirty
 */
GRK_API bool GRK_CALLCONV grk_decompress_set_progression_state(grk_object* codec,
                                                               grk_progression_state state);

/**
 * @brief Decompresses JPEG 2000 header
 * @param	codec				decompression codec (see @ref grk_object)
 * @param	header_info			information read from JPEG 2000 header (see @ref grk_header_info)
 * @return true	f the main header of the code stream and the JP2 header	is correctly read.
 */
GRK_API bool GRK_CALLCONV grk_decompress_read_header(grk_object* codec,
                                                     grk_header_info* header_info);

/**
 * @brief Gets decompressed tile image
 * @param	codec				decompression codec (see @ref grk_object)
 * @param	tile_index			tile index
 * @return pointer to @ref grk_image
 */
GRK_API grk_image* GRK_CALLCONV grk_decompress_get_tile_image(grk_object* codec,
                                                              uint16_t tile_index, bool wait);

/**
 * @brief Gets decompressed image
 * @param	codec	decompression codec (see @ref grk_object)
 * @return pointer to @ref grk_image
 */
GRK_API grk_image* GRK_CALLCONV grk_decompress_get_image(grk_object* codec);

/**
 * @brief Decompresses image from a JPEG 2000 code stream
 * @param codec 	decompression codec (see @ref grk_object)
 * @param tile		tile struct from plugin (see @ref grk_plugin_tile)
 * @return 			true if successful, otherwise false
 * */
GRK_API bool GRK_CALLCONV grk_decompress(grk_object* codec, grk_plugin_tile* tile);

/**
 * @brief Waits for an asynchronous decompression to complete
 * @param codec codec @ref grk_object
 * @param swath @ref grk_wait_swath to wait for. If null, then wait for
 * entire decompression to complete
 */
GRK_API void GRK_CALLCONV grk_decompress_wait(grk_object* codec, grk_wait_swath* swath);

/**
 * @brief Decompresses a specific tile
 * @param	codec			decompression codec (see @ref grk_object)
 * @param	tile_index		index of the tile to be decompressed
 * @return					true if successful, otherwise false
 */
GRK_API bool GRK_CALLCONV grk_decompress_tile(grk_object* codec, uint16_t tile_index);

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
  bool verbose; /* verbose */
  bool shared_memory_interface; /* shared memory interface */
} grk_cparameters;

/**
 * @brief Sets compression parameters to default values:
 * Lossless
 * Single tile
 * Size of precinct : 2^15 x 2^15
 * Size of code block : 64 x 64
 * Number of resolutions: 6
 * No SOP marker in the code stream
 * No EPH marker in the code stream
 * No mode switches
 * Progression order: LRCP
 * No ROI upshifted
 * Image origin lies at (0,0)
 * Tile origin lies at (0,0)
 * Reversible DWT 5-3 transform

 @param parameters Compression parameters (see @ref grk_cparameters)
 */
GRK_API void GRK_CALLCONV grk_compress_set_default_params(grk_cparameters* parameters);

/**
 * @brief Initializes compression process.
 * @param stream_params Stream parameters (see @ref grk_stream_params)
 * @param parameters  Compression parameters (see @ref grk_cparameters)
 * @param image Input image (see @ref grk_image)
 * @return pointer to initialized codec.
 */
GRK_API grk_object* GRK_CALLCONV grk_compress_init(grk_stream_params* stream_params,
                                                   grk_cparameters* parameters, grk_image* image);
/**
 * @brief Compresses an image into a JPEG 2000 code stream using plugin
 * @param codec compression codec (see @ref grk_object)
 * @param tile	plugin tile (see @ref grk_plugin_tile)
 * @return number of bytes written if successful, 0 otherwise
 */
GRK_API uint64_t GRK_CALLCONV grk_compress(grk_object* codec, grk_plugin_tile* tile);

/**
 * @brief Dumps codec information to file
 * @param	codec	decompression codec (see @ref grk_object)
 * @param	info_flag	type of information dump.
 * @param	output_stream	codec information is dumped to output stream
 */
GRK_API void GRK_CALLCONV grk_dump_codec(grk_object* codec, uint32_t info_flag,
                                         FILE* output_stream);

/**
 * @brief Sets MCT matrix
 *
 * @param	parameters		compression parameters (see @ref grk_cparameters)
 * @param	encoding_matrix	matrix
 * @param	dc_shift		dc shift coefficients to use
 * @param	nb_comp			number of components of the image.
 * @return	true if matrix was successfully set
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
 *   Example: rsiz = GRK_PROFILE_BC_MULTI | 0x0005; //level equals 5
 *
 * For IMF profiles, the GRK_PROFILE_X value has to be combined with the target main-level
 * (3-0 LSB, value between 0 and 11) and sub-level (7-4 LSB, value between 0 and 9):
 *   Example: rsiz = GRK_PROFILE_IMF_2K | 0x0040 | 0x0005; // main-level equals 5 and sub-level
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
  bool verbose; /* verbose */
} grk_plugin_load_info;

/**
 * @brief Loads plugin
 *
 * @param info		plugin loading info (see @ref grk_plugin_load_info)
 */
GRK_API bool GRK_CALLCONV grk_plugin_load(grk_plugin_load_info info);

/**
 * @brief Cleans up plugin resources
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
 * @brief Gets debug state of plugin
 */
GRK_API uint32_t GRK_CALLCONV grk_plugin_get_debug_state();

/*
 * @struct grk_plugin_init_info
 * @brief Plugin init info
 */
typedef struct _grk_plugin_init_info
{
  int32_t device_id; /* device ID */
  bool verbose; /* verbose */
  const char* license; /* license */
  const char* server; /* server */
} grk_plugin_init_info;

/**
 * @brief Initializes plugin
 * @param init_info plugin init info (see @ref grk_plugin_init_info)
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
 * @brief Compresses with plugin
 * @param compress_parameters 	compress parameters (see @ref grk_cparameters)
 * @param callback				callback (see @ref GRK_PLUGIN_COMPRESS_USER_CALLBACK)
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_compress(grk_cparameters* compress_parameters,
                                                 GRK_PLUGIN_COMPRESS_USER_CALLBACK callback);

/**
 * @brief Batch-compresses with plugin
 * @param info	batch compress info (see @ref grk_plugin_compress_batch_info)
 * @return 0 if successful
 *
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_batch_compress(grk_plugin_compress_batch_info info);

/**
 * @brief Waits for batch job to complete
 */
GRK_API void GRK_CALLCONV grk_plugin_wait_for_batch_complete(void);

/**
 * @brief Stops batch compress
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
 * @brief Decompresses single image with plugin
 * @param decompress_parameters  decompress parameters (see @ref grk_decompress_parameters)
 * @param callback  			 callback (see @ref grk_plugin_decompress_callback)
 * @return 0 if successful, otherwise return error code
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_decompress(grk_decompress_parameters* decompress_parameters,
                                                   grk_plugin_decompress_callback callback);

/**
 * @brief Initializes batch decompress with plugin
 * @param input_dir input directory holding compressed images
 * @param output_dir output directory holding decompressed images
 * @param decompress_parameters  decompress parameters (see @ref grk_decompress_parameters)
 * @param callback  			 callback (see @ref grk_plugin_decompress_callback)
 * @return 0 if successful, otherwise return error code
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_init_batch_decompress(
    const char* input_dir, const char* output_dir, grk_decompress_parameters* decompress_parameters,
    grk_plugin_decompress_callback callback);

/**
 * @brief Initiates batch decompress
 * @return 0 if successful, otherwise return error code
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_batch_decompress(void);

/**
 * @brief Stops batch decompress
 */
GRK_API void GRK_CALLCONV grk_plugin_stop_batch_decompress(void);

#ifndef SWIG
#ifdef __cplusplus
}
#endif
#endif
