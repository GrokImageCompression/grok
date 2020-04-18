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

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "openjpeg.h"

#include "minpf_plugin.h"


////////////////////////////////////////////////////
// Structs to pass data between plugin and open jpeg
////////////////////////////////////////////////////

typedef struct opj_pass {
    double distortionDecrease;  //distortion decrease up to and including this pass
    size_t rate;    // rate up to and including this pass
    size_t length;	//stream length for this pass
} opj_pass_t;

typedef struct opj_code_block {

    /////////////////////////
    // debug info
    int32_t x0, y0, x1, y1;
    unsigned int* contextStream;
    ///////////////////////////


    size_t numPix;
    unsigned char* compressedData;
    size_t compressedDataLength;
    size_t numBitPlanes;
    size_t numPasses;
    opj_pass_t passes[67];
    unsigned int sortedIndex;
} opj_code_block_t;

typedef struct opj_precinct {
    size_t numBlocks;
    opj_code_block_t** blocks;
} opj_precinct_t;

typedef struct opj_band {
    size_t orient;
    size_t numPrecincts;
    opj_precinct_t** precincts;
    float stepsize;
} opj_band_t;

typedef struct opj_resolution {
    size_t level;
    size_t numBands;
    opj_band_t** bands;

} opj_resolution_t;


typedef struct opj_tile_component {
    size_t numResolutions;
    opj_resolution_t** resolutions;
} opj_tile_component_t;

typedef struct opj_tile {
    size_t numComponents;
    opj_tile_component_t** tileComponents;
} opj_tile_t;



/////////////////////
// Debug Interface
/////////////////////

#define XIU_PLUGIN_STATE_NO_DEBUG			0x0
#define XIU_PLUGIN_STATE_DEBUG_ENCODE		0x1
#define XIU_PLUGIN_STATE_PRE_TR1			0x2
#define XIU_PLUGIN_STATE_DWT_QUANTIZATION	0x4
#define XIU_PLUGIN_STATE_MCT_ONLY			0x8
#define XIU_PLUGIN_STATE_CPU_ONLY			0x10

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


