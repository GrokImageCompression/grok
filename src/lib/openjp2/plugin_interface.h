/**
 *    Copyright (C) 2016-2019 Grok Image Compression Inc.
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

#include "grok.h"
#include <cstring>
#include "minpf_plugin.h"
#include <string>

#pragma once

namespace grk {

/////////////////////
// Debug Interface
/////////////////////

#define DEBUG_CONTEXT_CACHE_SIZE 3

// debugging variables
struct grk_plugin_debug_mqc {
	uint32_t debug_state;
	uint8_t context_number;
	uint32_t *contextStream;
	uint32_t contextStreamByteCount;
	uint8_t contextCache[DEBUG_CONTEXT_CACHE_SIZE];
	uint32_t contextCacheCount;
	uint8_t orient;
	uint32_t compno;
	uint32_t level;
};

typedef uint32_t (*PLUGIN_GET_DEBUG_STATE)(void);

typedef void (*PLUGIN_DEBUG_MQC_NEXT_CXD)(grk_plugin_debug_mqc *mqc, uint32_t d);

typedef void (*PLUGIN_DEBUG_MQC_NEXT_PLANE)(grk_plugin_debug_mqc *mqc);

/////////////////////
// encoder interface
/////////////////////

struct plugin_encode_user_callback_info {
	const char *input_file_name;
	bool outputFileNameIsRelative;
	const char *output_file_name;
	 grk_cparameters  *encoder_parameters;
	grk_image *image;
	grk_plugin_tile *tile;
	int32_t error_code;
};

typedef void (*PLUGIN_ENCODE_USER_CALLBACK)(
		plugin_encode_user_callback_info *info);

typedef bool (*PLUGIN_INIT)(grok_plugin_init_info_t initInfo);

typedef int32_t (*PLUGIN_ENCODE)( grk_cparameters  *encoding_parameters,
		PLUGIN_ENCODE_USER_CALLBACK callback);

typedef int32_t (*PLUGIN_BATCH_ENCODE)(const char *input_dir,
		const char *output_dir,  grk_cparameters  *encoding_parameters,
		PLUGIN_ENCODE_USER_CALLBACK userCallback);

typedef void (*PLUGIN_STOP_BATCH_ENCODE)(void);

typedef bool (*PLUGIN_IS_BATCH_COMPLETE)(void);

////////////////////
// decoder interface
////////////////////

struct PluginDecodeCallbackInfo {
	PluginDecodeCallbackInfo() :
			PluginDecodeCallbackInfo("", "", nullptr, -1, 0) {
	}
	PluginDecodeCallbackInfo(std::string input, std::string output,
			grk_decompress_parameters *decoderParameters, int format,
			uint32_t flags) :
			deviceId(0), init_decoders_func(nullptr), inputFile(input), outputFile(
					output), decod_format(format), cod_format(-1), l_stream(
					nullptr), l_codec(nullptr), decoder_parameters(
					decoderParameters), image(nullptr), plugin_owns_image(
					false), tile(nullptr), error_code(0), decode_flags(flags) {
		memset(&header_info, 0, sizeof(header_info));
	}
	size_t deviceId;
	GROK_INIT_DECODERS init_decoders_func;
	std::string inputFile;
	std::string outputFile;
	// input file format 0: J2K, 1: JP2
	int decod_format;
	// output file format 0: PGX, 1: PxM, 2: BMP etc 
	int cod_format;
	 grk_stream  *l_stream;
	 grk_codec  *l_codec;
	grk_decompress_parameters *decoder_parameters;
	 grk_header_info  header_info;
	grk_image *image;
	bool plugin_owns_image;
	grk_plugin_tile *tile;
	int32_t error_code;
	uint32_t decode_flags;
};

typedef int32_t (*PLUGIN_DECODE_USER_CALLBACK)(PluginDecodeCallbackInfo *info);

typedef int32_t (*PLUGIN_DECODE)(grk_decompress_parameters *decoding_parameters,
		PLUGIN_DECODE_USER_CALLBACK userCallback);

typedef int32_t (*PLUGIN_INIT_BATCH_DECODE)(const char *input_dir,
		const char *output_dir, grk_decompress_parameters *decoding_parameters,
		PLUGIN_DECODE_USER_CALLBACK userCallback);

typedef int32_t (*PLUGIN_BATCH_DECODE)(void);

typedef void (*PLUGIN_STOP_BATCH_DECODE)(void);

}

