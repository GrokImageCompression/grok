/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "grk_config.h"
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>

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

/**
 * Progression order
 * */
typedef enum _GRK_PROG_ORDER
{
   GRK_PROG_UNKNOWN = -1, /**< place-holder */
   GRK_LRCP = 0, /**< layer-resolution-component-precinct order */
   GRK_RLCP = 1, /**< resolution-layer-component-precinct order */
   GRK_RPCL = 2, /**< resolution-precinct-component-layer order */
   GRK_PCRL = 3, /**< precinct-component-resolution-layer order */
   GRK_CPRL = 4, /**< component-precinct-resolution-layer order */
   GRK_NUM_PROGRESSION_ORDERS = 5 /** number of possible progression orders */
} GRK_PROG_ORDER;

/**
 * Supported color spaces
 * */
typedef enum _GRK_COLOR_SPACE
{
   GRK_CLRSPC_UNKNOWN = 0, /**< unknown */
   GRK_CLRSPC_SRGB = 2, /**< sRGB */
   GRK_CLRSPC_GRAY = 3, /**< grayscale */
   GRK_CLRSPC_SYCC = 4, /**< standard YCC (YUV) */
   GRK_CLRSPC_EYCC = 5, /**< extended YCC */
   GRK_CLRSPC_CMYK = 6, /**< CMYK */
   GRK_CLRSPC_DEFAULT_CIE = 7, /**< default CIE LAB */
   GRK_CLRSPC_CUSTOM_CIE = 8, /**< custom CIE LAB */
   GRK_CLRSPC_ICC = 9 /**< ICC profile */
} GRK_COLOR_SPACE;

/* JPEG 2000 standard colour space enumeration */
typedef enum _GRK_ENUM_COLOUR_SPACE
{
   GRK_ENUM_CLRSPC_UNKNOWN = 0xFFFFFFFF,
   GRK_ENUM_CLRSPC_BILEVEL1 = 0,
   GRK_ENUM_CLRSPC_YCBCR1 = 1,
   GRK_ENUM_CLRSPC_YCBCR2 = 3,
   GRK_ENUM_CLRSPC_YCBCR3 = 4,
   GRK_ENUM_CLRSPC_PHOTO_YCC = 9, /* Kodak PhotoYCC */
   GRK_ENUM_CLRSPC_CMY = 11, /* cyan, magenta, yellow */
   GRK_ENUM_CLRSPC_CMYK = 12, /* cyan, magenta, yellow, black */
   GRK_ENUM_CLRSPC_YCCK = 13,
   GRK_ENUM_CLRSPC_CIE = 14,
   GRK_ENUM_CLRSPC_BILEVEL2 = 15,
   GRK_ENUM_CLRSPC_SRGB = 16,
   GRK_ENUM_CLRSPC_GRAY = 17,
   GRK_ENUM_CLRSPC_SYCC = 18, /* standard YCC */
   GRK_ENUM_CLRSPC_CIEJAB = 19,
   GRK_ENUM_CLRSPC_ESRGB = 20,
   GRK_ENUM_CLRSPC_ROMMRGB = 21, /* Reference Output Medium Metric RGB */
   GRK_ENUM_CLRSPC_YPBPR60 = 22,
   GRK_ENUM_CLRSPC_YPBPR50 = 23,
   GRK_ENUM_CLRSPC_EYCC = 24, /* extended YCC */
} GRK_ENUM_COLOUR_SPACE;

#define GRK_NUM_COMMENTS_SUPPORTED 256
#define GRK_NUM_ASOC_BOXES_SUPPORTED 256
#define GRK_MAX_COMMENT_LENGTH (UINT16_MAX - 2)
#define GRK_MAX_SUPPORTED_IMAGE_PRECISION 16 /* Maximum supported precision in library */

/* BIBO analysis - extra bits needed to avoid overflow:

 Lossless:
 without colour transform: 4 extra bits
 with colour transform:    5 extra bits

 Lossy:

 Need 1 extra bit

 So,  worst-case scenario is lossless with colour transform : need to add 5 more bits to prec to
 avoid overflow
 */
#define BIBO_EXTRA_BITS 7

#define GRK_MAX_PASSES (3 * (GRK_MAX_SUPPORTED_IMAGE_PRECISION + BIBO_EXTRA_BITS) - 2)

/**
 * Logging callback
 *
 * @param msg               message
 * @param client_data       client data passed to callback
 * */
typedef void (*grk_msg_callback)(const char* msg, void* client_data);

/**
 *
 * Grok ref-counted object
 *
 */
typedef struct _grk_object
{
   void* wrapper;
} grk_object;

/**
 * Progression order change
 *
 */
typedef struct _grk_progression
{
   GRK_PROG_ORDER progression;
   char progressionString[5];
   GRK_PROG_ORDER specifiedCompressionPocProg;
   uint32_t tileno;
   /** tile dimensions */
   uint32_t tx0;
   uint32_t ty0;
   uint32_t tx1;
   uint32_t ty1;
   /** progression order bounds specified by POC */
   uint16_t compS;
   uint16_t compE;
   uint8_t resS;
   uint8_t resE;
   uint64_t precS;
   uint64_t precE;
   uint16_t layS;
   uint16_t layE;
   uint16_t tpCompS;
   uint16_t tpCompE;
   uint8_t tpResS;
   uint8_t tpResE;
   uint64_t tpPrecE;
   uint16_t tpLayE;
   uint32_t tp_txS;
   uint32_t tp_txE;
   uint32_t tp_tyS;
   uint32_t tp_tyE;
   uint32_t dx;
   uint32_t dy;
   uint16_t comp_temp;
   uint8_t res_temp;
   uint64_t prec_temp;
   uint16_t lay_temp;
   uint32_t tx0_temp;
   uint32_t ty0_temp;
} grk_progression;

/** RAW component compress parameters */
typedef struct _grk_raw_comp_cparameters
{
   uint8_t dx; /** subsampling in X direction */
   uint8_t dy; /** subsampling in Y direction */
   /*@}*/
} grk_raw_comp_cparameters;

/**RAW image compress parameters */
typedef struct _grk_raw_cparameters
{
   uint32_t width; /** width of the raw image */
   uint32_t height; /** height of the raw image */
   uint16_t numcomps; /** number of components of the raw image */
   uint8_t prec; /** bit depth of the raw image */
   bool sgnd; /** signed/unsigned raw image */
   grk_raw_comp_cparameters* comps; /** raw components parameters */
} grk_raw_cparameters;

/**
 * Rate control algorithms
  GRK_RATE_CONTROL_BISECT: bisect with all truncation points
  GRK_RATE_CONTROL_PCRD_OPT: bisect with only feasible truncation points
 */
typedef enum _GRK_RATE_CONTROL_ALGORITHM
{
   GRK_RATE_CONTROL_BISECT,
   GRK_RATE_CONTROL_PCRD_OPT
} GRK_RATE_CONTROL_ALGORITHM;

/**
 * All supported file formats
 */
typedef enum _GRK_SUPPORTED_FILE_FMT
{
   GRK_FMT_UNK,
   GRK_FMT_J2K,
   GRK_FMT_JP2,
   GRK_FMT_PXM,
   GRK_FMT_PGX,
   GRK_FMT_PAM,
   GRK_FMT_BMP,
   GRK_FMT_TIF,
   GRK_FMT_RAW, /* MSB / Big Endian */
   GRK_FMT_PNG,
   GRK_FMT_RAWL, /* LSB / Little Endian */
   GRK_FMT_JPG
} GRK_SUPPORTED_FILE_FMT;

/**
 * Supported JPEG 2000 formats
 */
typedef enum _GRK_CODEC_FORMAT
{
   GRK_CODEC_UNK, /**< unknown format */
   GRK_CODEC_J2K, /**< JPEG 2000 code stream format */
   GRK_CODEC_JP2 /**< JP2 file format */
} GRK_CODEC_FORMAT;

#define GRK_PATH_LEN 4096 /* Maximum allowed filename size */
#define GRK_MAX_LAYERS 100 /* Maximum number of quality layers */

/*
 * Note: range for number of decomposition levels is 0-32
 * So, accordingly, range for number of resolutions is 1-33
 */
#define GRK_J2K_MAX_DECOMP_LVLS                                     \
   32 /* Maximum number of decomposition levels allowed by standard \
	   */
#define GRK_J2K_MAXRLVLS \
   (GRK_J2K_MAX_DECOMP_LVLS + 1) /* Maximum number of resolution levels allowed by standard*/
#define GRK_J2K_MAXBANDS \
   (3 * GRK_J2K_MAXRLVLS - 2) /*  Maximum number of sub-bands allowed by standard */

/**
 * Note: "component" refers to an image component as decompressed
 * from the code stream, while "channel" refers to a component resulting
 * from the application of a Palette box LUT and a Component mapping box.
 */

/**
 Component mappings: component index, mapping type, palette column
 */
typedef struct _grk_component_mapping_comp
{
   uint16_t component_index;
   uint8_t mapping_type;
   uint8_t palette_column;
} grk_component_mapping_comp;

/**
 Palette data
 */
typedef struct _grk_palette_data
{
   int32_t* lut;
   uint16_t num_entries;
   grk_component_mapping_comp* component_mapping;
   uint8_t num_channels;
   bool* channel_sign;
   uint8_t* channel_prec;
} grk_palette_data;

/***
 * Channel Definition box structures and enums.
 * When no Component mapping box is present, it is still possible to have
 * a Channel defintion box, in which case channels are associated with components
 * in the obvious way : channel `k` corresponds to component `k`.
 * */

/* channel type */
typedef enum _GRK_CHANNEL_TYPE
{
   GRK_CHANNEL_TYPE_COLOUR = 0,
   GRK_CHANNEL_TYPE_OPACITY = 1,
   GRK_CHANNEL_TYPE_PREMULTIPLIED_OPACITY = 2,
   GRK_CHANNEL_TYPE_UNSPECIFIED = 65535U

} GRK_CHANNEL_TYPE;

/* channel association */
typedef enum _GRK_CHANNEL_ASSOC
{

   GRK_CHANNEL_ASSOC_WHOLE_IMAGE = 0,
   GRK_CHANNEL_ASSOC_COLOUR_1 = 1,
   GRK_CHANNEL_ASSOC_COLOUR_2 = 2,
   GRK_CHANNEL_ASSOC_COLOUR_3 = 3,
   GRK_CHANNEL_ASSOC_UNASSOCIATED = 65535U

} GRK_CHANNEL_ASSOC;

/**
 Channel definition: channel index, type, association
 */
typedef struct _grk_channel_description
{
   uint16_t channel;
   uint16_t typ;
   uint16_t asoc;
} grk_channel_description;

/**
 Channel definitions and number of definitions
 */
typedef struct _grk_channel_definition
{
   grk_channel_description* descriptions;
   uint16_t num_channel_descriptions;
} grk_channel_definition;

/**
 ICC profile, palette, channel definition
 */
typedef struct _grk_color
{
   uint8_t* icc_profile_buf;
   uint32_t icc_profile_len;
   char* icc_profile_name;
   grk_channel_definition* channel_definition;
   grk_palette_data* palette;
   bool has_colour_specification_box;
} grk_color;

/**
 * Association box info
 */
typedef struct _grk_asoc
{
   uint32_t level; /* 0 for root level */
   const char* label;
   uint8_t* xml;
   uint32_t xml_len;
} grk_asoc;

/**
 * Precision mode
 */
typedef enum _grk_precision_mode
{
   GRK_PREC_MODE_CLIP,
   GRK_PREC_MODE_SCALE
} grk_precision_mode;

/**
 * Precision
 */
typedef struct _grk_precision
{
   uint8_t prec;
   grk_precision_mode mode;
} grk_precision;

/**
 * Header info
 */
typedef struct _grk_header_info
{
   /******************************************
   set by client only if decompressing to file
   *******************************************/
   GRK_SUPPORTED_FILE_FMT decompressFormat;
   bool forceRGB;
   bool upsample;
   grk_precision* precision;
   uint32_t numPrecision;
   bool splitByComponent;
   bool singleTileDecompress;
   /****************************************/

   /*****************************************
   populated by library after reading header
   ******************************************/
   /** initial code block width, default to 64 */
   uint32_t cblockw_init;
   /** initial code block height, default to 64 */
   uint32_t cblockh_init;
   /** 1 : use the irreversible DWT 9-7, 0 : use lossless compression (default) */
   bool irreversible;
   /** multi-component transform identifier */
   uint32_t mct;
   /** RSIZ value
	To be used to combine GRK_PROFILE_*, GRK_EXTENSION_* and (sub)levels values. */
   uint16_t rsiz;
   /** number of resolutions */
   uint32_t numresolutions;
   /*********************************************************
   coding style can be specified in main header COD segment,
   tile header COD segment, and tile component COC segment.
   *********************************************************/
   /* !!!! assume that coding style does not vary across tile components */
   uint8_t csty;
   /*******************************************************************
   code block style is specified in main header COD segment, and can
   be overridden in a tile header.  !!! Assume that style does
   not vary across tiles !!!
   *******************************************************************/
   uint8_t cblk_sty;
   /** initial precinct width */
   uint32_t prcw_init[GRK_J2K_MAXRLVLS];
   /** initial precinct height */
   uint32_t prch_init[GRK_J2K_MAXRLVLS];
   /** XTOsiz */
   uint32_t tx0;
   /** YTOsiz */
   uint32_t ty0;
   /** XTsiz */
   uint32_t t_width;
   /** YTsiz */
   uint32_t t_height;
   /** tile grid width */
   uint32_t t_grid_width;
   /** tile grid height  */
   uint32_t t_grid_height;
   /** maximum number of layers */
   uint16_t max_layers_;
   /*************************************
   note: xml_data will remain valid
	until codec is destroyed
	************************************/
   uint8_t* xml_data;
   size_t xml_data_len;
   size_t num_comments;
   char* comment[GRK_NUM_COMMENTS_SUPPORTED];
   uint16_t comment_len[GRK_NUM_COMMENTS_SUPPORTED];
   bool isBinaryComment[GRK_NUM_COMMENTS_SUPPORTED];

   grk_asoc asocs[GRK_NUM_ASOC_BOXES_SUPPORTED];
   uint32_t num_asocs;
} grk_header_info;

typedef struct _grk_io_buf
{
   uint8_t* data_;
   size_t offset_;
   size_t len_;
   size_t allocLen_;
   bool pooled_;
   uint32_t index_;
} grk_io_buf;

typedef struct _grk_io_init
{
   uint32_t maxPooledRequests_;

} grk_io_init;

typedef bool (*grk_io_callback)(uint32_t threadId, grk_io_buf buffer, void* io_user_data);
typedef void (*grk_io_register_reclaim_callback)(grk_io_init io_init,
												 grk_io_callback reclaim_callback,
												 void* io_user_data, void* reclaim_user_data);
typedef bool (*grk_io_pixels_callback)(uint32_t threadId, grk_io_buf buffer, void* user_data);

/**
 * read stream callback
 *
 * @buffer buffer to write stream to
 * @numBytes number of bytes to write to buffer
 * @user_data user data
 *
 */
typedef size_t (*grk_stream_read_fn)(uint8_t* buffer, size_t numBytes, void* user_data);

/**
 * write stream callback
 *
 * @buffer buffer to read stream from
 * @numBytes number of bytes to read from buffer
 * @user_data user data
 *
 */
typedef size_t (*grk_stream_write_fn)(const uint8_t* buffer, size_t numBytes, void* user_data);

/**
 * seek (absolute) callback
 *
 * @offset absolute stream offset
 * @user_data user data
 *
 */
typedef bool (*grk_stream_seek_fn)(uint64_t offset, void* user_data);

/**
 * free user data callback
 *
 * @user_data user data
 *
 */
typedef void (*grk_stream_free_user_data_fn)(void* user_data);

/**
 * JPEG 2000 stream parameters. Client must populate one of the following options :
 * 1. File
 * 2. Buffer
 * 3. Callback
 */
typedef struct _grk_stream_params
{
   /* 1. File */
   const char* file;

   /* 2. Buffer */
   uint8_t* buf;
   size_t buf_len;
   /* length of compressed stream (set by compressor, not client) */
   size_t buf_compressed_len;

   /* 3. Callback */
   grk_stream_read_fn read_fn;
   grk_stream_write_fn write_fn;
   grk_stream_seek_fn seek_fn;
   grk_stream_free_user_data_fn free_user_data_fn; // optional
   void* user_data;
   size_t stream_len; // must be set for read stream
   size_t double_buffer_len; // optional - default value is 1024 * 1024
} grk_stream_params;

typedef enum _GRK_TILE_CACHE_STRATEGY
{
   GRK_TILE_CACHE_NONE, /* no tile caching */
   GRK_TILE_CACHE_IMAGE /* cache final tile image */
} GRK_TILE_CACHE_STRATEGY;

/**
 * Core decompression parameters
 * */
typedef struct _grk_decompress_core_params
{
   /**
	Set the number of highest resolution levels to be discarded.
	The image resolution is effectively divided by 2 to the power of the number of discarded
	levels. The reduce factor is limited by the smallest total number of decomposition levels among
	tiles. if greater than zero, then image is decoded to original dimension divided by
	2^(reduce); if equal to zero or not used, image is decompressed to full resolution
	*/
   uint8_t reduce;
   /**
	Set the maximum number of quality layers to decompress.
	If there are fewer quality layers than the specified number, all quality layers will be
	decompressed. if != 0, then only the first "layer" layers are decompressed; if == 0 or not
	used, all the quality layers are decompressed
	*/
   uint16_t layers_to_decompress_;
   GRK_TILE_CACHE_STRATEGY tileCacheStrategy;

   uint32_t randomAccessFlags_;

   grk_io_pixels_callback io_buffer_callback;
   void* io_user_data;
   grk_io_register_reclaim_callback io_register_client_callback;
} grk_decompress_core_params;

#define GRK_DECOMPRESS_COMPRESSION_LEVEL_DEFAULT (UINT_MAX)

/**
 * Decompression parameters
 */
typedef struct _grk_decompress_params
{
   /** core library parameters */
   grk_decompress_core_params core;
   /** input file name */
   char infile[GRK_PATH_LEN];
   /** output file name */
   char outfile[GRK_PATH_LEN];
   /** input file format*/
   GRK_CODEC_FORMAT decod_format;
   /** output file format*/
   GRK_SUPPORTED_FILE_FMT cod_format;
   /** Decompress window left boundary */
   float dw_x0;
   /** Decompress window right boundary */
   float dw_x1;
   /** Decompress window up boundary */
   float dw_y0;
   /** Decompress window bottom boundary */
   float dw_y1;
   /** tile number of the decompressed tile*/
   uint16_t tileIndex;
   bool singleTileDecompress;
   grk_precision* precision;
   uint32_t numPrecision;
   /* force output colorspace to RGB */
   bool force_rgb;
   /* upsample components according to their dx/dy values */
   bool upsample;
   /* split output components to different files */
   bool split_pnm;
   /* serialize XML metadata to disk */
   bool io_xml;
   uint32_t compression;
   /*****************************************************
   compression "quality". Meaning of "quality" depends
   on file format we are writing to
   *****************************************************/
   uint32_t compressionLevel;
   /** Verbose mode */
   bool verbose_;
   int32_t deviceId;
   uint32_t duration; /* in seconds */
   uint32_t kernelBuildOptions;
   uint32_t repeats;
   uint32_t numThreads;
   void* user_data;
} grk_decompress_parameters;

/**
 * Image component
 * */
typedef struct _grk_image_comp
{
   /** x component offset compared to the whole image */
   uint32_t x0;
   /** y component offset compared to the whole image */
   uint32_t y0;
   /** data width */
   uint32_t w;
   /** data stride */
   uint32_t stride;
   /** data height */
   uint32_t h;
   /** XRsiz: horizontal separation of a sample of component with respect to the reference
	* grid */
   uint8_t dx;
   /** YRsiz: vertical separation of a sample of component with respect to the reference grid
	*/
   uint8_t dy;
   /** precision */
   uint8_t prec;
   /* signed */
   bool sgnd;
   GRK_CHANNEL_TYPE type;
   GRK_CHANNEL_ASSOC association;
   /* component registration coordinates */
   uint16_t Xcrg, Ycrg;
   /** image component data */
   int32_t* data;
} grk_image_comp;

/* Image meta data: colour, IPTC and XMP */
typedef struct _grk_image_meta
{
   grk_object obj;
   grk_color color;
   uint8_t* iptc_buf;
   size_t iptc_len;
   uint8_t* xmp_buf;
   size_t xmp_len;
} grk_image_meta;

typedef struct _grk_image
{
   grk_object obj;
   /** XOsiz: horizontal offset from the origin of the reference grid
	*  to the left side of the image area */
   uint32_t x0;
   /** YOsiz: vertical offset from the origin of the reference grid
	*  to the top side of the image area */
   uint32_t y0;
   /** Xsiz: width of the reference grid */
   uint32_t x1;
   /** Ysiz: height of the reference grid */
   uint32_t y1;
   /** number of components in the image */
   uint16_t numcomps;
   GRK_COLOR_SPACE color_space;
   bool paletteApplied_;
   bool channelDefinitionApplied_;
   bool has_capture_resolution;
   double capture_resolution[2];
   bool has_display_resolution;
   double display_resolution[2];
   GRK_SUPPORTED_FILE_FMT decompressFormat;
   bool forceRGB;
   bool upsample;
   grk_precision* precision;
   uint32_t numPrecision;
   bool hasMultipleTiles;
   bool splitByComponent;
   uint16_t decompressNumComps;
   uint32_t decompressWidth;
   uint32_t decompressHeight;
   uint8_t decompressPrec;
   GRK_COLOR_SPACE decompressColourSpace;
   grk_io_buf interleavedData;
   uint32_t rowsPerStrip; // for storage to output format
   uint32_t rowsPerTask; // for scheduling
   uint64_t packedRowBytes;
   grk_image_meta* meta;
   grk_image_comp* comps;
} grk_image;

/*************************************************
Structs to pass data between grok and plugin
************************************************/
/**
 * Plugin pass
 */
typedef struct _grk_plugin_pass
{
   double distortionDecrease; /* distortion decrease up to and including this pass */
   size_t rate; /* rate up to and including this pass */
   size_t length; /* stream length for this pass */
} grk_plugin_pass;

/**
 * Plugin code block
 */
typedef struct _grk_plugin_code_block
{
   /**************************
   debug info
   **************************/
   uint32_t x0, y0, x1, y1;
   unsigned int* contextStream;
   /***************************/
   uint32_t numPix;
   uint8_t* compressedData;
   uint32_t compressedDataLength;
   uint8_t numBitPlanes;
   size_t numPasses;
   grk_plugin_pass passes[GRK_MAX_PASSES];
   unsigned int sortedIndex;
} grk_plugin_code_block;

/**
 * Plugin precinct
 */
typedef struct _grk_plugin_precinct
{
   uint64_t numBlocks;
   grk_plugin_code_block** blocks;
} grk_plugin_precinct;

/**
 * Plugin band
 */
typedef struct _grk_plugin_band
{
   uint8_t orientation;
   uint64_t numPrecincts;
   grk_plugin_precinct** precincts;
   float stepsize;
} grk_plugin_band;

/**
 * Plugin resolution
 */
typedef struct _grk_plugin_resolution
{
   size_t level;
   size_t numBands;
   grk_plugin_band** band;
} grk_plugin_resolution;

/**
 * Plugin tile component
 */
typedef struct grk_plugin_tile_component
{
   size_t numResolutions;
   grk_plugin_resolution** resolutions;
} grk_plugin_tile_component;

#define GRK_DECODE_HEADER (1 << 0)
#define GRK_DECODE_T2 (1 << 1)
#define GRK_DECODE_T1 (1 << 2)
#define GRK_DECODE_POST_T1 (1 << 3)
#define GRK_PLUGIN_DECODE_CLEAN (1 << 4)
#define GRK_DECODE_ALL                                                            \
   (GRK_PLUGIN_DECODE_CLEAN | GRK_DECODE_HEADER | GRK_DECODE_T2 | GRK_DECODE_T1 | \
	GRK_DECODE_POST_T1)

/**
 * Plugin tile
 */
typedef struct _grk_plugin_tile
{
   uint32_t decompress_flags;
   size_t numComponents;
   grk_plugin_tile_component** tileComponents;
} grk_plugin_tile;

/* opaque codec object */
typedef grk_object grk_codec;

/**
 * Library version
 */
GRK_API const char* GRK_CALLCONV grk_version(void);

/**
 * Initialize library
 *
 * @param pluginPath 	path to plugin
 * @param numthreads 	number of threads to use for compress/decompress
 */
GRK_API void GRK_CALLCONV grk_initialize(const char* pluginPath, uint32_t numthreads, bool verbose);

/**
 * De-initialize library
 */
GRK_API void GRK_CALLCONV grk_deinitialize();

/**
 * Increment ref count
 */
GRK_API grk_object* GRK_CALLCONV grk_object_ref(grk_object* obj);

/*
 * Decrement ref count
 *
 */
GRK_API void GRK_CALLCONV grk_object_unref(grk_object* obj);

GRK_API void GRK_CALLCONV grk_set_msg_handlers(grk_msg_callback info_callback, void* info_user_data,
											   grk_msg_callback warn_callback, void* warn_user_data,
											   grk_msg_callback error_callback,
											   void* error_user_data);

/**
 * Create image
 *
 * @param numcmpts      number of components
 * @param cmptparms     component parameters
 * @param clrspc        image color space
 * @param alloc_data    if true, allocate component data buffers
 *
 * @return returns      a new image if successful, otherwise NULL
 * */
GRK_API grk_image* GRK_CALLCONV grk_image_new(uint16_t numcmpts, grk_image_comp* cmptparms,
											  GRK_COLOR_SPACE clrspc, bool alloc_data);

GRK_API grk_image_meta* GRK_CALLCONV grk_image_meta_new(void);

/**
 * Detect jpeg 2000 format from file
 * Format is either GRK_FMT_J2K or GRK_FMT_JP2
 *
 * @param fileName file name
 * @param fmt pointer to detected format
 *
 * @return true if format was detected, otherwise false
 *
 */
GRK_API bool GRK_CALLCONV grk_decompress_detect_format(const char* fileName, GRK_CODEC_FORMAT* fmt);

/**
 * Initialize stream parameters with default values
 *
 * @param parameters stream parameters
 */
GRK_API void GRK_CALLCONV grk_set_default_stream_params(grk_stream_params* params);

/**
 * Initialize decompress parameters with default values
 *
 * @param parameters decompression parameters
 */
GRK_API void GRK_CALLCONV grk_decompress_set_default_params(grk_decompress_parameters* parameters);

/**
 * Initialize decompressor
 *
 * @param stream_params 	source stream parameters
 * @param core_params 	decompress core parameters
 *
 * @return grk_codec* if successful, otherwise NULL
 */
GRK_API grk_codec* GRK_CALLCONV grk_decompress_init(grk_stream_params* stream_params,
													grk_decompress_core_params* core_params);

/**
 * Decompress JPEG 2000 header
 *
 * @param	codec				decompression codec
 * @param	header_info			information read from JPEG 2000 header.
 *
 * @return true					if the main header of the code stream and the JP2 header
 * 							 	is correctly read.
 */
GRK_API bool GRK_CALLCONV grk_decompress_read_header(grk_codec* codec,
													 grk_header_info* header_info);

/**
 * Get decompressed tile image
 *
 * @param	codec				decompression codec
 * @param	tileIndex			tile index
 *
 * @return pointer to decompressed image
 */
GRK_API grk_image* GRK_CALLCONV grk_decompress_get_tile_image(grk_codec* codec, uint16_t tileIndex);

/**
 * Get decompressed composite image
 *
 * @param	codec				decompression codec
 *
 * @return pointer to decompressed image
 */
GRK_API grk_image* GRK_CALLCONV grk_decompress_get_composited_image(grk_codec* codec);

/**
 * Set the given area to be decompressed. This function should be called
 * right after grk_decompress_read_header is called, and before any tile header is read.
 *
 * @param	codec			decompression codec
 * @param	start_x		    left position of the rectangle to decompress (in image coordinates).
 * @param	end_x			the right position of the rectangle to decompress (in image
 * coordinates).
 * @param	start_y		    up position of the rectangle to decompress (in image coordinates).
 * @param	end_y			bottom position of the rectangle to decompress (in image coordinates).
 *
 * @return	true			if the area could be set.
 */
GRK_API bool GRK_CALLCONV grk_decompress_set_window(grk_codec* codec, float start_x, float start_y,
													float end_x, float end_y);

/**
 * Decompress image from a JPEG 2000 code stream
 *
 * @param codec 	decompression codec
 * @param tile		tile struct from plugin
 *
 * @return 			true if successful, otherwise false
 * */
GRK_API bool GRK_CALLCONV grk_decompress(grk_codec* codec, grk_plugin_tile* tile);

/**
 * Decompress a specific tile
 *
 * @param	codec			decompression codec
 * @param	tileIndex		index of the tile to be decompressed
 *
 * @return					true if successful, otherwise false
 */
GRK_API bool GRK_CALLCONV grk_decompress_tile(grk_codec* codec, uint16_t tileIndex);

/* COMPRESSION FUNCTIONS*/

/**
 * Compress parameters
 * */
typedef struct _grk_cparameters
{
   /** size of tile: tile_size_on = false (not in argument) or = true (in argument) */
   bool tile_size_on;
   /** XTOsiz */
   uint32_t tx0;
   /** YTOsiz */
   uint32_t ty0;
   /** XTsiz */
   uint32_t t_width;
   /** YTsiz */
   uint32_t t_height;
   /** number of layers */
   uint16_t numlayers;
   /** rate control allocation by rate/distortion curve */
   bool allocationByRateDistoration;
   /** layers rates expressed as compression ratios.
	*  They might be subsequently limited by the max_cs_size field */
   double layer_rate[GRK_MAX_LAYERS];
   /** rate control allocation by fixed_PSNR quality */
   bool allocationByQuality;
   /** layer PSNR values */
   double layer_distortion[GRK_MAX_LAYERS];
   char* comment[GRK_NUM_COMMENTS_SUPPORTED];
   uint16_t comment_len[GRK_NUM_COMMENTS_SUPPORTED];
   bool is_binary_comment[GRK_NUM_COMMENTS_SUPPORTED];
   size_t num_comments;
   /** csty : coding style */
   uint8_t csty;
   /* number of guard bits */
   uint8_t numgbits;
   /** progression order (default is LRCP) */
   GRK_PROG_ORDER prog_order;
   /** progressions */
   grk_progression progression[GRK_J2K_MAXRLVLS];
   /** number of progression order changes (POCs), default to 0 */
   uint32_t numpocs;
   /** number of resolutions */
   uint8_t numresolution;
   /** initial code block width  (default to 64) */
   uint32_t cblockw_init;
   /** initial code block height (default to 64) */
   uint32_t cblockh_init;
   /** code block style */
   uint8_t cblk_sty;
   /** 1 : use the irreversible DWT 9-7, 0 :
	*  use lossless compression (default) */
   bool irreversible;
   /** region of interest: affected component in [0..3];
	*  -1 indicates no ROI */
   int32_t roi_compno;
   /** region of interest: upshift value */
   uint32_t roi_shift;
   /* number of precinct size specifications */
   uint32_t res_spec;
   /** initial precinct width */
   uint32_t prcw_init[GRK_J2K_MAXRLVLS];
   /** initial precinct height */
   uint32_t prch_init[GRK_J2K_MAXRLVLS];
   /** input file name */
   char infile[GRK_PATH_LEN];
   /** output file name */
   char outfile[GRK_PATH_LEN];
   /** subimage compressing: origin image offset in x direction */
   uint32_t image_offset_x0;
   /** subimage compressing: origin image offset in y direction */
   uint32_t image_offset_y0;
   /** subsampling value for dx */
   uint8_t subsampling_dx;
   /** subsampling value for dy */
   uint8_t subsampling_dy;
   /** input file format*/
   GRK_SUPPORTED_FILE_FMT decod_format;
   /** output file format*/
   GRK_SUPPORTED_FILE_FMT cod_format;
   grk_raw_cparameters raw_cp;
   /** Tile part generation*/
   bool enableTilePartGeneration;
   /** new tile part progression divider */
   uint8_t newTilePartProgressionDivider;
   /** MCT (multiple component transform) */
   uint8_t mct;
   /** Naive implementation of MCT restricted to a single reversible array based
	compressing without offset concerning all the components. */
   void* mct_data;
   /**
	* Maximum size (in bytes) for the whole code stream.
	* If equal to zero, code stream size limitation is not considered
	* If it does not comply with layer_rate, max_cs_size prevails
	* and a warning is issued.
	* */
   uint64_t max_cs_size;
   /**
	* Maximum size (in bytes) for each component.
	* If == 0, component size limitation is not considered
	* */
   uint64_t max_comp_size;
   /** RSIZ value
	To be used to combine GRK_PROFILE_*, GRK_EXTENSION_* and (sub)levels values. */
   uint16_t rsiz;
   uint16_t framerate;

   /* set to true if input file stores capture resolution */
   bool write_capture_resolution_from_file;
   double capture_resolution_from_file[2];

   bool write_capture_resolution;
   double capture_resolution[2];

   bool write_display_resolution;
   double display_resolution[2];

   bool apply_icc_;

   GRK_RATE_CONTROL_ALGORITHM rateControlAlgorithm;
   uint32_t numThreads;
   int32_t deviceId;
   uint32_t duration; /* seconds */
   uint32_t kernelBuildOptions;
   uint32_t repeats;
   bool writePLT;
   bool writeTLM;
   bool verbose;
   bool sharedMemoryInterface;
} grk_cparameters;

/**
 Set compressing parameters to default values:

 Lossless
 Single tile
 Size of precinct : 2^15 x 2^15 (i.e. single precinct)
 Size of code block : 64 x 64
 Number of resolutions: 6
 No SOP marker in the code stream
 No EPH marker in the code stream
 No mode switches
 Progression order: LRCP
 No ROI upshifted
 Image origin lies at (0,0)
 Tile origin lies at (0,0)
 Reversible DWT 5-3 transform

 @param parameters Compression parameters
 */
GRK_API void GRK_CALLCONV grk_compress_set_default_params(grk_cparameters* parameters);

/**
 * Set up the compressor parameters using the current image and user parameters.
 *
 * @param codec 		compression codec
 * @param parameters 	compression parameters
 * @param image 		input image
 */
GRK_API grk_codec* GRK_CALLCONV grk_compress_init(grk_stream_params* stream_params,
												  grk_cparameters* parameters, grk_image* p_image);

/**
 * Compress an image into a JPEG 2000 code stream using plugin
 *
 * @param codec 		compression codec
 * @param tile			plugin tile
 *
 * @return 				number of bytes written if successful, 0 otherwise
 */
GRK_API uint64_t GRK_CALLCONV grk_compress(grk_codec* codec, grk_plugin_tile* tile);

/**
 * Dump codec information to file
 *
 * @param	codec			decompression codec
 * @param	info_flag		type of information dump.
 * @param	output_stream	codec information is dumped to output stream
 *
 */
GRK_API void GRK_CALLCONV grk_dump_codec(grk_codec* codec, uint32_t info_flag, FILE* output_stream);

/**
 * Set the MCT matrix to use.
 *
 * @param	parameters		the parameters to change.
 * @param	encodingMatrix	the compressing matrix.
 * @param	dc_shift		the dc shift coefficients to use.
 * @param	nbComp			the number of components of the image.
 *
 * @return	true if the parameters could be set.
 */
GRK_API bool GRK_CALLCONV grk_set_MCT(grk_cparameters* parameters, float* encodingMatrix,
									  int32_t* dc_shift, uint32_t nbComp);

#define GRK_IMG_INFO 1 /* Basic image information provided to the user */
#define GRK_J2K_MH_INFO 2 /* Codestream information based only on the main header */
#define GRK_J2K_TH_INFO 4 /* Tile information based on the current tile header */
#define GRK_J2K_TCH_INFO 8 /**< Tile/Component information of all tiles */
#define GRK_J2K_MH_IND 16 /**< Codestream index based only on the main header */
#define GRK_J2K_TH_IND 32 /**< Tile index based on the current tile */
#define GRK_JP2_INFO 128 /**< JP2 file information */
#define GRK_JP2_IND 256 /**< JP2 file index */

#define GRK_CBLKSTY_LAZY 0x01 /**< Selective arithmetic coding bypass */
#define GRK_CBLKSTY_RESET 0x02 /**< Reset context probabilities on coding pass boundaries */
#define GRK_CBLKSTY_TERMALL 0x04 /**< Termination on each coding pass */
#define GRK_CBLKSTY_VSC 0x08 /**< Vertical stripe causal context */
#define GRK_CBLKSTY_PTERM 0x10 /**< Predictable termination */
#define GRK_CBLKSTY_SEGSYM 0x20 /**< Segmentation symbols are used */
#define GRK_CBLKSTY_HT 0x40 /**< high throughput block coding only */
#define GRK_CBLKSTY_HT_MIXED 0xC0 /**< high throughput block coding - mixed*/
#define GRK_JPH_RSIZ_FLAG 0x4000 /**<for JPH, bit 14 of RSIZ must be set to 1 */

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
#define GRK_PROFILE_PART2_EXTENSIONS_MASK 0x3FFF // Mask for Part-2 extension bits

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

#define GRK_IS_BROADCAST(v)                                                         \
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

#define GRK_IS_IMF(v)                                                          \
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

#define GRK_CINEMA_4K_DEFAULT_NUM_RESOLUTIONS 7

/*
 *
 * CIE Lab #defines
 */
#define GRK_CUSTOM_CIELAB_SPACE 0x0
#define GRK_DEFAULT_CIELAB_SPACE 0x44454600 /* 'DEF' */
#define GRK_CIE_DAY ((((uint32_t)'C') << 24) + (((uint32_t)'T') << 16))
#define GRK_CIE_D50 ((uint32_t)0x00443530)
#define GRK_CIE_D65 ((uint32_t)0x00443635)
#define GRK_CIE_D75 ((uint32_t)0x00443735)
#define GRK_CIE_SA ((uint32_t)0x00005341)
#define GRK_CIE_SC ((uint32_t)0x00005343)
#define GRK_CIE_F2 ((uint32_t)0x00004632)
#define GRK_CIE_F7 ((uint32_t)0x00004637)
#define GRK_CIE_F11 ((uint32_t)0x00463131)

/**
 * Toggle random access markers
 */
#define GRK_RANDOM_ACCESS_PLT 1 /* use PLT marker if present */
#define GRK_RANDOM_ACCESS_TLM 2 /* use TLM marker if present */
#define GRK_RANDOM_ACCESS_PLM 4 /* use PLM marker if present */

/*************************************************************************************
 Plugin Interface
 *************************************************************************************/

/*
 Plugin management
 */

typedef struct _grk_plugin_load_info
{
   const char* pluginPath;
   bool verbose;
} grk_plugin_load_info;

/**
 * Load plugin
 *
 * @param info		plugin loading info
 */
GRK_API bool GRK_CALLCONV grk_plugin_load(grk_plugin_load_info info);

/**
 * Release plugin resources
 */
GRK_API void GRK_CALLCONV grk_plugin_cleanup(void);

/* No debug is done on plugin. Production setting. */
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
 * Get debug state of plugin
 */
GRK_API uint32_t GRK_CALLCONV grk_plugin_get_debug_state();

/*
 Plugin compressing
 */
typedef struct _grk_plugin_init_info
{
   int32_t deviceId;
   bool verbose;
   const char* license;
   const char* server;
} grk_plugin_init_info;

/**
 * Initialize plugin
 */
GRK_API bool GRK_CALLCONV grk_plugin_init(grk_plugin_init_info initInfo);

typedef struct grk_plugin_compress_user_callback_info
{
   const char* input_file_name;
   bool outputFileNameIsRelative;
   const char* output_file_name;
   grk_cparameters* compressor_parameters;
   grk_image* image;
   grk_plugin_tile* tile;
   grk_stream_params stream_params;
   unsigned int error_code;
   bool transferExifTags;
} grk_plugin_compress_user_callback_info;

typedef uint64_t (*GRK_PLUGIN_COMPRESS_USER_CALLBACK)(grk_plugin_compress_user_callback_info* info);

typedef struct grk_plugin_compress_batch_info
{
   const char* input_dir;
   const char* output_dir;
   grk_cparameters* compress_parameters;
   GRK_PLUGIN_COMPRESS_USER_CALLBACK callback;
} grk_plugin_compress_batch_info;

/**
 * Compress with plugin
 *
 * @param compress_parameters 	compress parameters
 * @param callback				callback
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_compress(grk_cparameters* compress_parameters,
												 GRK_PLUGIN_COMPRESS_USER_CALLBACK callback);

/**
 * Batch compress with plugin
 *
 * @param input_dir				directory holding input images
 * @param output_dir			directory holding compressed output images
 * @param compress_parameters 	compress parameters
 * @param callback				callback
 *
 * @return 0 if successful
 *
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_batch_compress(grk_plugin_compress_batch_info info);

/**
 * Wait for batch job to complete
 */
GRK_API void GRK_CALLCONV grk_plugin_wait_for_batch_complete(void);

/**
 * Stop batch compress
 */
GRK_API void GRK_CALLCONV grk_plugin_stop_batch_compress(void);

/*
 Plugin decompression
 */

typedef int (*GROK_INIT_DECOMPRESSORS)(grk_header_info* header_info, grk_image* image);

typedef struct _grk_plugin_decompress_callback_info
{
   size_t deviceId;
   GROK_INIT_DECOMPRESSORS init_decompressors_func;
   const char* input_file_name;
   const char* output_file_name;
   /* input file format 0: J2K, 1: JP2 */
   GRK_CODEC_FORMAT decod_format;
   /* output file format 0: PGX, 1: PxM, 2: BMP etc */
   GRK_SUPPORTED_FILE_FMT cod_format;
   grk_codec* codec;
   grk_header_info header_info;
   grk_decompress_parameters* decompressor_parameters;
   grk_image* image;
   bool plugin_owns_image;
   grk_plugin_tile* tile;
   unsigned int error_code;
   uint32_t decompress_flags;
   uint32_t full_image_x0;
   uint32_t full_image_y0;
   void* user_data;
} grk_plugin_decompress_callback_info;

typedef int32_t (*grk_plugin_decompress_callback)(grk_plugin_decompress_callback_info* info);

/**
 * Decompress with plugin
 *
 * @param decompress_parameters  decompress parameters
 * @param callback  			 callback
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_decompress(grk_decompress_parameters* decompress_parameters,
												   grk_plugin_decompress_callback callback);

/**
 * Initialize batch decompress
 *
 * @param input_dir input directory holding compressed images
 * @param output_dir output directory holding decompressed images
 * @param decompress_parameters  decompress parameters
 * @param callback  			 callback
 *
 * @return 0 if successful
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_init_batch_decompress(
	const char* input_dir, const char* output_dir, grk_decompress_parameters* decompress_parameters,
	grk_plugin_decompress_callback callback);

/**
 * Initiate batch decompress
 */
GRK_API int32_t GRK_CALLCONV grk_plugin_batch_decompress(void);

/**
 * Stop batch decompress
 */
GRK_API void GRK_CALLCONV grk_plugin_stop_batch_decompress(void);

#ifdef __cplusplus
}
#endif
