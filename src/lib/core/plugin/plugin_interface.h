/**
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
 */

#include <plugin/minpf_plugin.h>
#include "grok.h"
#include <cstring>
#include <string>

#pragma once

namespace grk
{
/////////////////////
// Debug Interface
/////////////////////

#define DEBUG_CONTEXT_CACHE_SIZE 3

// debugging variables
struct grk_plugin_debug_mqc
{
   uint32_t debug_state;
   uint8_t context_number;
   uint32_t* context_stream;
   uint32_t contextStreamByteCount;
   uint8_t contextCache[DEBUG_CONTEXT_CACHE_SIZE];
   uint32_t contextCacheCount;
   uint8_t orientation;
   uint16_t compno;
   uint32_t level;
};

typedef uint32_t (*PLUGIN_GET_DEBUG_STATE)(void);

typedef void (*PLUGIN_DEBUG_MQC_NEXT_CXD)(grk_plugin_debug_mqc* mqc, uint32_t d);

typedef void (*PLUGIN_DEBUG_MQC_NEXT_PLANE)(grk_plugin_debug_mqc* mqc);

/////////////////////
// compressor interface
/////////////////////

typedef bool (*PLUGIN_INIT)(grk_plugin_init_info initInfo);

typedef int32_t (*PLUGIN_ENCODE)(grk_cparameters* encoding_parameters,
								 GRK_PLUGIN_COMPRESS_USER_CALLBACK callback);

typedef int32_t (*PLUGIN_BATCH_ENCODE)(grk_plugin_compress_batch_info info);

typedef void (*PLUGIN_STOP_BATCH_ENCODE)(void);

typedef void (*PLUGIN_WAIT_FOR_BATCH_COMPLETE)(void);

////////////////////
// decompressor interface
////////////////////

struct PluginDecodeCallbackInfo
{
   PluginDecodeCallbackInfo() : PluginDecodeCallbackInfo("", "", nullptr, GRK_CODEC_UNK, 0) {}
   PluginDecodeCallbackInfo(std::string input, std::string output,
							grk_decompress_parameters* decompressorParameters,
							GRK_CODEC_FORMAT format, uint32_t flags)
	   : device_id(0), init_decompressors_func(nullptr), inputFile(input), outputFile(output),
		 decod_format(format), cod_format(GRK_FMT_UNK), codec(nullptr),
		 decompressor_parameters(decompressorParameters), image(nullptr), plugin_owns_image(false),
		 tile(nullptr), error_code(0), decompress_flags(flags), user_data(nullptr)

   {
	  memset(&header_info, 0, sizeof(header_info));
   }
   size_t device_id;
   GROK_INIT_DECOMPRESSORS init_decompressors_func;
   std::string inputFile;
   std::string outputFile;
   // input file format 0: J2K, 1: JP2
   GRK_CODEC_FORMAT decod_format;
   // output file format 0: PGX, 1: PxM, 2: BMP etc
   GRK_SUPPORTED_FILE_FMT cod_format;
   grk_codec* codec;
   grk_decompress_parameters* decompressor_parameters;
   grk_header_info header_info;
   grk_image* image;
   bool plugin_owns_image;
   grk_plugin_tile* tile;
   int32_t error_code;
   uint32_t decompress_flags;
   void* user_data;
};

typedef int32_t (*PLUGIN_DECODE_USER_CALLBACK)(PluginDecodeCallbackInfo* info);

typedef int32_t (*PLUGIN_DECODE)(grk_decompress_parameters* decoding_parameters,
								 PLUGIN_DECODE_USER_CALLBACK userCallback);

typedef int32_t (*PLUGIN_INIT_BATCH_DECODE)(const char* input_dir, const char* output_dir,
											grk_decompress_parameters* decoding_parameters,
											PLUGIN_DECODE_USER_CALLBACK userCallback);

typedef int32_t (*PLUGIN_BATCH_DECODE)(void);

typedef void (*PLUGIN_STOP_BATCH_DECODE)(void);

} // namespace grk
