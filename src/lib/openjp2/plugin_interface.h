/**
*    Copyright (C) 2016 Grok Image Compression Inc.
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

#include "openjpeg.h"
#include "minpf_plugin.h"



/////////////////////
// Debug Interface
/////////////////////


static const char* plugin_get_debug_state_method_name = "plugin_get_debug_state";
typedef uint32_t(*PLUGIN_GET_DEBUG_STATE)(void);


/////////////////////
// encoder interface
/////////////////////

typedef struct plugin_encode_user_callback_info {
    const char* input_file_name;
    const char* output_file_name;
    opj_cparameters_t* encoder_parameters;
    opj_image_t* image;
    opj_tile_t* tile;
    int32_t	error_code;
} plugin_encode_user_callback_info_t;

typedef void(*PLUGIN_ENCODE_USER_CALLBACK)(plugin_encode_user_callback_info_t* info);

static const char* plugin_encode_method_name = "plugin_encode";
typedef int32_t (*PLUGIN_ENCODE)( opj_cparameters_t* encoding_parameters, PLUGIN_ENCODE_USER_CALLBACK callback);


static const char* plugin_batch_encode_method_name = "plugin_batch_encode";
typedef int32_t (*PLUGIN_BATCH_ENCODE)(const char* input_dir,
                                       const char* output_dir,
                                       opj_cparameters_t* encoding_parameters,
                                       PLUGIN_ENCODE_USER_CALLBACK userCallback);

static const char* plugin_stop_batch_encode_method_name = "plugin_stop_batch_encode";
typedef void (*PLUGIN_STOP_BATCH_ENCODE)(void);




////////////////////
// decoder interface
////////////////////

typedef opj_tile_t*(*GENERATE_TILE)(size_t deviceId,
                                    size_t compressed_tile_id,
                                    opj_cparameters_t* encoder_parameters,
                                    opj_image_t* image);

typedef bool(*QUEUE_DECODE)(size_t deviceId,
                            size_t compressed_tile_id,
                            opj_tile_t* tile);

typedef struct plugin_decode_callback_info {
    size_t deviceId;
    size_t compressed_tile_id;
    GENERATE_TILE generate_tile_func;
    QUEUE_DECODE queue_decoder_func;
    const char* input_file_name;
    const char* output_file_name;
    opj_decompress_parameters* decoder_parameters;
    opj_image_t* image;
    opj_tile_t* tile;
    int32_t	error_code;
} plugin_decode_callback_info_t;

typedef void(*PLUGIN_DECODE_USER_CALLBACK)(plugin_decode_callback_info_t* info);


static const char* plugin_decode_method_name = "plugin_decode";
typedef int32_t (*PLUGIN_DECODE)(opj_decompress_parameters* decoding_parameters,
                                 PLUGIN_DECODE_USER_CALLBACK userCallback);


static const char* plugin_batch_decode_method_name = "plugin_batch_decode";
typedef int32_t (*PLUGIN_BATCH_DECODE)(const char* input_dir,
                                       const char* output_dir,
                                       opj_decompress_parameters* decoding_parameters,
                                       PLUGIN_DECODE_USER_CALLBACK userCallback);

static const char* plugin_stop_batch_decode_method_name = "plugin_stop_batch_decode";
typedef void(*PLUGIN_STOP_BATCH_DECODE)(void);


