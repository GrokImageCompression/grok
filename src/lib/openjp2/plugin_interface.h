/**
*    Copyright (C) 2016-2017 Grok Image Compression Inc.
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

#include "openjpeg.h"
#include "minpf_plugin.h"

#pragma once

namespace grk {
	

/////////////////////
// Debug Interface
/////////////////////

#define DEBUG_CONTEXT_CACHE_SIZE 3

// debugging variables
struct plugin_debug_mqc_t {
	uint32_t	debug_state;
	uint8_t		context_number;			
	uint32_t*	contextStream;			
	uint32_t	contextStreamByteCount;	
	uint8_t		contextCache[DEBUG_CONTEXT_CACHE_SIZE];
	uint32_t	contextCacheCount;
};



typedef uint32_t(*PLUGIN_GET_DEBUG_STATE)(void);



typedef void (*PLUGIN_DEBUG_MQC_NEXT_CXD)(plugin_debug_mqc_t *mqc, uint32_t d);



typedef void  (*PLUGIN_DEBUG_MQC_NEXT_PLANE)(plugin_debug_mqc_t *mqc);


/////////////////////
// encoder interface
/////////////////////

struct plugin_encode_user_callback_info_t {
    const char* input_file_name;
	bool	outputFileNameIsRelative;
    const char* output_file_name;
    opj_cparameters_t* encoder_parameters;
    opj_image_t* image;
    grok_plugin_tile_t* tile;
    int32_t	error_code;
} ;

typedef void(*PLUGIN_ENCODE_USER_CALLBACK)(plugin_encode_user_callback_info_t* info);


typedef bool(*PLUGIN_INIT)(opj_plugin_init_info_t initInfo);



typedef int32_t (*PLUGIN_ENCODE)( opj_cparameters_t* encoding_parameters, PLUGIN_ENCODE_USER_CALLBACK callback);



typedef int32_t (*PLUGIN_BATCH_ENCODE)(const char* input_dir,
                                       const char* output_dir,
                                       opj_cparameters_t* encoding_parameters,
                                       PLUGIN_ENCODE_USER_CALLBACK userCallback);


typedef void (*PLUGIN_STOP_BATCH_ENCODE)(void);



typedef bool (*PLUGIN_IS_BATCH_ENCODE_COMPLETE)(void);



////////////////////
// decoder interface
////////////////////

typedef grok_plugin_tile_t*(*GENERATE_TILE)(size_t deviceId,
                                    size_t compressed_tile_id,
                                    opj_cparameters_t* encoder_parameters,
                                    opj_image_t* image);

struct plugin_decode_callback_info_t {
    size_t deviceId;
    size_t compressed_tile_id;
    GENERATE_TILE generate_tile_func;
    const char* input_file_name;
    const char* output_file_name;
	opj_stream_t*				l_stream;
	opj_codec_t*				l_codec;
    opj_decompress_parameters* decoder_parameters;
    opj_image_t* image;
    grok_plugin_tile_t* tile;
    int32_t	error_code;
} ;

typedef void(*PLUGIN_DECODE_USER_CALLBACK)(plugin_decode_callback_info_t* info);



typedef int32_t (*PLUGIN_DECODE)(opj_decompress_parameters* decoding_parameters,
                                 PLUGIN_DECODE_USER_CALLBACK userCallback);



typedef int32_t (*PLUGIN_BATCH_DECODE)(const char* input_dir,
                                       const char* output_dir,
                                       opj_decompress_parameters* decoding_parameters,
                                       PLUGIN_DECODE_USER_CALLBACK userCallback);


typedef void(*PLUGIN_STOP_BATCH_DECODE)(void);



}

