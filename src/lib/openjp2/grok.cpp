/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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
*    This source code incorporates work covered by the following copyright and
*    permission notice:
*
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2008, 2011-2012, Centre National d'Etudes Spatiales (CNES), FR
 * Copyright (c) 2012, CS Systemes d'Information, France
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef _WIN32
#include <windows.h>
#else /* _WIN32 */

#include <errno.h>

#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
# include <unistd.h>
# include <sys/mman.h>

#endif

#include <fcntl.h>
#include "grok_includes.h"
using namespace grk;

static bool is_initialized = false;
bool OPJ_CALLCONV opj_initialize(const char* plugin_path){
    if (!is_initialized) {
		grok_plugin_load_info_t info;
		info.plugin_path = plugin_path;
        is_initialized = grok_plugin_load(info);
    }
    return is_initialized;
}

OPJ_API void OPJ_CALLCONV opj_cleanup() {
	grok_plugin_cleanup();
}

/* ---------------------------------------------------------------------- */
/* Functions to set the message handlers */

bool OPJ_CALLCONV opj_set_info_handler(	opj_codec_t * p_codec,
                                        opj_msg_callback p_callback,
                                        void * p_user_data){
    codec_private_t * l_codec = (codec_private_t *) p_codec;
    if(! l_codec) {
        return false;
    }

    l_codec->m_event_mgr.info_handler = p_callback;
    l_codec->m_event_mgr.m_info_data = p_user_data;

    return true;
}
bool OPJ_CALLCONV opj_set_warning_handler(	opj_codec_t * p_codec,
        opj_msg_callback p_callback,
        void * p_user_data){
    codec_private_t * l_codec = (codec_private_t *) p_codec;
    if (! l_codec) {
        return false;
    }
    l_codec->m_event_mgr.warning_handler = p_callback;
    l_codec->m_event_mgr.m_warning_data = p_user_data;
    return true;
}
bool OPJ_CALLCONV opj_set_error_handler(opj_codec_t * p_codec,
                                        opj_msg_callback p_callback,
                                        void * p_user_data){
    codec_private_t * l_codec = (codec_private_t *) p_codec;
    if (! l_codec) {
        return false;
    }
    l_codec->m_event_mgr.error_handler = p_callback;
    l_codec->m_event_mgr.m_error_data = p_user_data;
    return true;
}
/* ---------------------------------------------------------------------- */

static size_t grok_read_from_file (void * p_buffer, size_t p_nb_bytes, FILE * p_file){
    size_t l_nb_read = fread(p_buffer,1,p_nb_bytes,p_file);
    return l_nb_read ? l_nb_read : (size_t)-1;
}

static uint64_t opj_get_data_length_from_file (FILE * p_file){
     GROK_FSEEK(p_file, 0, SEEK_END);
	int64_t file_length = (int64_t)GROK_FTELL(p_file);
    GROK_FSEEK(p_file, 0, SEEK_SET);
    return (uint64_t)file_length;
}
static size_t grok_write_from_file (void * p_buffer, size_t p_nb_bytes, FILE * p_file){
    return fwrite(p_buffer,1,p_nb_bytes,p_file);
}
static bool grok_seek_from_file (int64_t p_nb_bytes, FILE * p_user_data){
	return GROK_FSEEK(p_user_data, p_nb_bytes, SEEK_SET) ? false : true;
}

/* ---------------------------------------------------------------------- */

/* ---------------------------------------------------------------------- */



#ifdef _WIN32
#ifndef OPJ_STATIC
BOOL APIENTRY
DllMain(HINSTANCE hModule, DWORD ul_reason_for_call, LPVOID lpReserved){
    ARG_NOT_USED(lpReserved);
    ARG_NOT_USED(hModule);
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH :
        break;
    case DLL_PROCESS_DETACH :
        break;
    case DLL_THREAD_ATTACH :
    case DLL_THREAD_DETACH :
        break;
    }
    return TRUE;
}
#endif /* OPJ_STATIC */
#endif /* _WIN32 */

/* ---------------------------------------------------------------------- */

const char* OPJ_CALLCONV opj_version(void){
    return OPJ_PACKAGE_VERSION;
}

/* ---------------------------------------------------------------------- */
/* DECOMPRESSION FUNCTIONS*/

opj_codec_t* OPJ_CALLCONV opj_create_decompress(OPJ_CODEC_FORMAT p_format){
    codec_private_t *l_codec = nullptr;
    l_codec = (codec_private_t*) grok_calloc(1, sizeof(codec_private_t));
    if (!l_codec) {
        return nullptr;
    }
    l_codec->is_decompressor = 1;

    switch (p_format) {
    case OPJ_CODEC_J2K:
        l_codec->opj_dump_codec = (void (*) (void*, int32_t, FILE*)) j2k_dump;

        l_codec->get_codec_info = (opj_codestream_info_v2_t* (*) (void*) ) j2k_get_cstr_info;

        l_codec->opj_get_codec_index = (opj_codestream_index_t* (*) (void*) ) j2k_get_cstr_index;

        l_codec->m_codec_data.m_decompression.decode =
            (bool (*) (	void *, 
						grok_plugin_tile_t*,
                        GrokStream *,
                        opj_image_t*, event_mgr_t * )) j2k_decode;

        l_codec->m_codec_data.m_decompression.end_decompress =
            (bool (*) (	void *,
                        GrokStream *,
                        event_mgr_t *)) j2k_end_decompress;

        l_codec->m_codec_data.m_decompression.read_header =
            (bool (*) (	GrokStream *,
                        void *,
						opj_header_info_t* header_info,
                        opj_image_t **,
                        event_mgr_t * )) j2k_read_header;

        l_codec->m_codec_data.m_decompression.destroy =
            (void (*) (void *))j2k_destroy;

        l_codec->m_codec_data.m_decompression.setup_decoder =
            (void (*) (void * , opj_dparameters_t * )) j2k_setup_decoder;

        l_codec->m_codec_data.m_decompression.read_tile_header =
            (bool (*) (	void *,
                        uint32_t*,
                        uint64_t*,
                        uint32_t*, uint32_t*,
                        uint32_t*, uint32_t*,
                        uint32_t*,
                        bool*,
                        GrokStream *,
                        event_mgr_t * )) j2k_read_tile_header;

        l_codec->m_codec_data.m_decompression.decode_tile_data =
            (bool (*) ( void *,
                        uint32_t,
                        uint8_t*,
                        uint64_t,
                        GrokStream *,
                        event_mgr_t *)) j2k_decode_tile;

        l_codec->m_codec_data.m_decompression.set_decode_area =
            (bool (*) ( void *,
                        opj_image_t*,
                        uint32_t, uint32_t, uint32_t, uint32_t,
                        event_mgr_t *)) j2k_set_decode_area;

        l_codec->m_codec_data.m_decompression.get_decoded_tile =
            (bool (*) ( void *p_codec,
                        GrokStream *p_cio,
                        opj_image_t *p_image,
                        event_mgr_t * p_manager,
                        uint32_t tile_index)) j2k_get_tile;

        l_codec->m_codec_data.m_decompression.set_decoded_resolution_factor =
            (bool (*) ( void * p_codec,
                        uint32_t res_factor,
                        event_mgr_t * p_manager)) j2k_set_decoded_resolution_factor;

        l_codec->m_codec = j2k_create_decompress();

        if (! l_codec->m_codec) {
            grok_free(l_codec);
            return nullptr;
        }
        break;
    case OPJ_CODEC_JP2:
        /* get a JP2 decoder handle */
        l_codec->opj_dump_codec = (void (*) (void*, int32_t, FILE*)) jp2_dump;
        l_codec->get_codec_info = (opj_codestream_info_v2_t* (*) (void*) ) jp2_get_cstr_info;
        l_codec->opj_get_codec_index = (opj_codestream_index_t* (*) (void*) ) jp2_get_cstr_index;
        l_codec->m_codec_data.m_decompression.decode =
            (bool (*) (	void *, 
						grok_plugin_tile_t*,
                        GrokStream *,
                        opj_image_t*,
                        event_mgr_t * )) jp2_decode;
        l_codec->m_codec_data.m_decompression.end_decompress =
            (bool (*) ( void *,
                        GrokStream *,
                        event_mgr_t *)) jp2_end_decompress;
        l_codec->m_codec_data.m_decompression.read_header =
            (bool (*) ( GrokStream *,
                        void *,
						opj_header_info_t* header_info,
                        opj_image_t **,
                        event_mgr_t * )) jp2_read_header;
        l_codec->m_codec_data.m_decompression.read_tile_header =
            (bool (*) ( void *,
                        uint32_t*,
                        uint64_t*,
                        uint32_t*,
                        uint32_t*,
                        uint32_t * ,
                        uint32_t * ,
                        uint32_t * ,
                        bool *,
                        GrokStream *,
                        event_mgr_t * )) jp2_read_tile_header;
        l_codec->m_codec_data.m_decompression.decode_tile_data =
            (bool (*) ( void *,
                        uint32_t,
						uint8_t*,
						uint64_t,
                        GrokStream *,
                        event_mgr_t * )) jp2_decode_tile;

        l_codec->m_codec_data.m_decompression.destroy = (void (*) (void *))jp2_destroy;
        l_codec->m_codec_data.m_decompression.setup_decoder =
            (void (*) (void * ,opj_dparameters_t * )) jp2_setup_decoder;
        l_codec->m_codec_data.m_decompression.set_decode_area =
            (bool (*) ( void *,
                        opj_image_t*,
                        uint32_t,uint32_t,uint32_t,uint32_t,
                        event_mgr_t * )) jp2_set_decode_area;

        l_codec->m_codec_data.m_decompression.get_decoded_tile =
            (bool (*) ( void *p_codec,
                        GrokStream *p_cio,
                        opj_image_t *p_image,
                        event_mgr_t * p_manager,
                        uint32_t tile_index)) jp2_get_tile;

        l_codec->m_codec_data.m_decompression.set_decoded_resolution_factor =
            (bool (*) ( void * p_codec,
                        uint32_t res_factor,
                        event_mgr_t * p_manager)) jp2_set_decoded_resolution_factor;

        l_codec->m_codec = jp2_create(true);
        if (! l_codec->m_codec) {
            grok_free(l_codec);
            return nullptr;
        }
        break;
    case OPJ_CODEC_UNKNOWN:
    default:
        grok_free(l_codec);
        return nullptr;
    }
    set_default_event_handler(&(l_codec->m_event_mgr));
    return (opj_codec_t*) l_codec;
}
void OPJ_CALLCONV opj_set_default_decoder_parameters(opj_dparameters_t *parameters){
    if(parameters) {
        memset(parameters, 0, sizeof(opj_dparameters_t));
        /* default decoding parameters */
        parameters->cp_layer = 0;
        parameters->cp_reduce = 0;
        parameters->decod_format = -1;
        parameters->cod_format = -1;
        parameters->flags = 0;
		parameters->numThreads = 8;
    }
}
bool OPJ_CALLCONV opj_setup_decoder(opj_codec_t *p_codec,
                                    opj_dparameters_t *parameters ){
    if (p_codec && parameters) {
        codec_private_t * l_codec = (codec_private_t *) p_codec;
        if (! l_codec->is_decompressor) {
            event_msg(&(l_codec->m_event_mgr), EVT_ERROR,
                          "Codec provided to the opj_setup_decoder function is not a decompressor handler.\n");
            return false;
        }
        l_codec->m_codec_data.m_decompression.setup_decoder(l_codec->m_codec,
                parameters);
        return true;
    }
    return false;
}
bool OPJ_CALLCONV opj_read_header(opj_stream_t *p_stream,
	opj_codec_t *p_codec,
	opj_image_t **p_image) {
	return opj_read_header_ex(p_stream, p_codec, nullptr, p_image);
}
bool OPJ_CALLCONV opj_read_header_ex (	opj_stream_t *p_stream,
                                    opj_codec_t *p_codec,
									opj_header_info_t* header_info,
                                    opj_image_t **p_image ){
    if (p_codec && p_stream) {
        codec_private_t* l_codec = (codec_private_t*) p_codec;
        GrokStream* l_stream = (GrokStream*) p_stream;
        if(! l_codec->is_decompressor) {
            event_msg(&(l_codec->m_event_mgr), EVT_ERROR,
                          "Codec provided to the grok_read_header function is not a decompressor handler.\n");
            return false;
        }

        return l_codec->m_codec_data.m_decompression.read_header(	l_stream,
                l_codec->m_codec,
				header_info,
                p_image,
                &(l_codec->m_event_mgr) );
    }
    return false;
}
bool OPJ_CALLCONV opj_decode(opj_codec_t *p_codec,
	opj_stream_t *p_stream,
	opj_image_t* p_image) {
	return opj_decode_ex(p_codec, nullptr, p_stream, p_image);
}
bool OPJ_CALLCONV opj_decode_ex(   opj_codec_t *p_codec,
								grok_plugin_tile_t* tile,
                                opj_stream_t *p_stream,
                                opj_image_t* p_image){
    if (p_codec && p_stream) {
        codec_private_t * l_codec = (codec_private_t *) p_codec;
        GrokStream * l_stream = (GrokStream *) p_stream;
        if (! l_codec->is_decompressor) {
            return false;
        }
        return l_codec->m_codec_data.m_decompression.decode(l_codec->m_codec,
				tile,
                l_stream,
                p_image,
                &(l_codec->m_event_mgr) );
    }
    return false;
}
bool OPJ_CALLCONV opj_set_decode_area(	opj_codec_t *p_codec,
                                        opj_image_t* p_image,
                                        uint32_t p_start_x, uint32_t p_start_y,
                                        uint32_t p_end_x, uint32_t p_end_y){
    if (p_codec) {
        codec_private_t * l_codec = (codec_private_t *) p_codec;
        if (! l_codec->is_decompressor) {
            return false;
        }
        return  l_codec->m_codec_data.m_decompression.set_decode_area(	l_codec->m_codec,
                p_image,
                p_start_x, p_start_y,
                p_end_x, p_end_y,
                &(l_codec->m_event_mgr) );
    }
    return false;
}
bool OPJ_CALLCONV opj_read_tile_header(	opj_codec_t *p_codec,
                                        opj_stream_t * p_stream,
                                        uint32_t * p_tile_index,
                                        uint64_t * p_data_size,
                                        uint32_t * p_tile_x0,
										uint32_t * p_tile_y0,
                                        uint32_t * p_tile_x1,
										uint32_t * p_tile_y1,
                                        uint32_t * p_nb_comps,
                                        bool * p_should_go_on){
    if (p_codec && p_stream && p_data_size && p_tile_index) {
        codec_private_t * l_codec = (codec_private_t *) p_codec;
        GrokStream * l_stream = (GrokStream *) p_stream;
        if (! l_codec->is_decompressor) {
            return false;
        }
        return l_codec->m_codec_data.m_decompression.read_tile_header(	l_codec->m_codec,
                p_tile_index,
                p_data_size,
                p_tile_x0, p_tile_y0,
                p_tile_x1, p_tile_y1,
                p_nb_comps,
                p_should_go_on,
                l_stream,
                &(l_codec->m_event_mgr));
    }
    return false;
}
bool OPJ_CALLCONV opj_decode_tile_data(	opj_codec_t *p_codec,
                                        uint32_t p_tile_index,
                                        uint8_t * p_data,
                                        uint64_t p_data_size,
                                        opj_stream_t *p_stream){
    if (p_codec && p_data && p_stream) {
        codec_private_t * l_codec = (codec_private_t *) p_codec;
        GrokStream * l_stream = (GrokStream *) p_stream;

        if (! l_codec->is_decompressor) {
            return false;
        }

        return l_codec->m_codec_data.m_decompression.decode_tile_data(	l_codec->m_codec,
                p_tile_index,
                p_data,
                p_data_size,
                l_stream,
                &(l_codec->m_event_mgr) );
    }
    return false;
}
bool OPJ_CALLCONV opj_get_decoded_tile(	opj_codec_t *p_codec,
                                        opj_stream_t *p_stream,
                                        opj_image_t *p_image,
                                        uint32_t tile_index){
    if (p_codec && p_stream) {
        codec_private_t * l_codec = (codec_private_t *) p_codec;
        GrokStream * l_stream = (GrokStream *) p_stream;

        if (! l_codec->is_decompressor) {
            return false;
        }

        return l_codec->m_codec_data.m_decompression.get_decoded_tile(	l_codec->m_codec,
                l_stream,
                p_image,
                &(l_codec->m_event_mgr),
                tile_index);
    }
    return false;
}
bool OPJ_CALLCONV opj_set_decoded_resolution_factor(opj_codec_t *p_codec,
													uint32_t res_factor ){
    codec_private_t * l_codec = (codec_private_t *) p_codec;
    if ( !l_codec ) {
        return false;
    }
    return l_codec->m_codec_data.m_decompression.set_decoded_resolution_factor(l_codec->m_codec,
            res_factor,
            &(l_codec->m_event_mgr) );
}

/* ---------------------------------------------------------------------- */
/* COMPRESSION FUNCTIONS*/

opj_codec_t* OPJ_CALLCONV opj_create_compress(OPJ_CODEC_FORMAT p_format){
    codec_private_t *l_codec = nullptr;
    l_codec = (codec_private_t*)grok_calloc(1, sizeof(codec_private_t));
    if (!l_codec) {
        return nullptr;
    }
    l_codec->is_decompressor = 0;

    switch(p_format) {
    case OPJ_CODEC_J2K:
        l_codec->m_codec_data.m_compression.encode = (bool (*) (void *,
				grok_plugin_tile_t*,
			GrokStream *,
			grk::event_mgr_t * )) j2k_encode;
        l_codec->m_codec_data.m_compression.end_compress = (bool (*) (	void *,
                GrokStream *,
                grk::event_mgr_t *)) j2k_end_compress;
        l_codec->m_codec_data.m_compression.start_compress = (bool (*) (void *,
			GrokStream *,
                opj_image_t * ,
			grk::event_mgr_t *)) j2k_start_compress;
        l_codec->m_codec_data.m_compression.write_tile = (bool (*) (void *,
                uint32_t,
                uint8_t*,
                uint64_t,
			GrokStream *,
			grk::event_mgr_t *) ) j2k_write_tile;
        l_codec->m_codec_data.m_compression.destroy = (void (*) (void *)) j2k_destroy;
        l_codec->m_codec_data.m_compression.setup_encoder = (bool (*) (	void *,
                opj_cparameters_t *,
                opj_image_t *,
			grk::event_mgr_t * )) j2k_setup_encoder;
        l_codec->m_codec = j2k_create_compress();
        if (! l_codec->m_codec) {
            grok_free(l_codec);
            return nullptr;
        }
        break;
    case OPJ_CODEC_JP2:
        /* get a JP2 decoder handle */
        l_codec->m_codec_data.m_compression.encode = (bool (*) (void *,
				grok_plugin_tile_t*,
			GrokStream *,
			grk::event_mgr_t * )) jp2_encode;
        l_codec->m_codec_data.m_compression.end_compress = (bool (*) (	void *,
                GrokStream *,
                event_mgr_t *)) jp2_end_compress;
        l_codec->m_codec_data.m_compression.start_compress = (bool (*) (void *,
                GrokStream *,
                opj_image_t * ,
                event_mgr_t *))  jp2_start_compress;
        l_codec->m_codec_data.m_compression.write_tile = (bool (*) (void *,
                uint32_t,
                uint8_t*,
                uint64_t,
                GrokStream *,
                event_mgr_t *)) jp2_write_tile;
        l_codec->m_codec_data.m_compression.destroy = (void (*) (void *)) jp2_destroy;
        l_codec->m_codec_data.m_compression.setup_encoder = (bool (*) (	void *,
                opj_cparameters_t *,
                opj_image_t *,
                event_mgr_t * )) jp2_setup_encoder;

        l_codec->m_codec = jp2_create(false);
        if (! l_codec->m_codec) {
            grok_free(l_codec);
            return nullptr;
        }
        break;
    case OPJ_CODEC_UNKNOWN:
    default:
        grok_free(l_codec);
        return nullptr;
    }
    set_default_event_handler(&(l_codec->m_event_mgr));
    return (opj_codec_t*) l_codec;
}
void OPJ_CALLCONV opj_set_default_encoder_parameters(opj_cparameters_t *parameters){
    if(parameters) {
        memset(parameters, 0, sizeof(opj_cparameters_t));
        /* default coding parameters */
        parameters->rsiz = OPJ_PROFILE_NONE;
        parameters->max_comp_size = 0;
        parameters->numresolution = 6;
        parameters->cblockw_init = 64;
        parameters->cblockh_init = 64;
        parameters->prog_order = OPJ_LRCP;
        parameters->roi_compno = -1;		/* no ROI */
        parameters->subsampling_dx = 1;
        parameters->subsampling_dy = 1;
        parameters->tp_on = 0;
        parameters->decod_format = -1;
        parameters->cod_format = -1;
        parameters->tcp_rates[0] = 0;
        parameters->tcp_numlayers = 0;
        parameters->cp_disto_alloc = 0;
        parameters->cp_fixed_quality = 0;
		parameters->numThreads = 8;
		parameters->deviceId = 0;
		parameters->repeats = 1;
    }
}
bool OPJ_CALLCONV opj_setup_encoder(opj_codec_t *p_codec,
                                    opj_cparameters_t *parameters,
                                    opj_image_t *p_image){
    if (p_codec && parameters && p_image) {
        codec_private_t * l_codec = (codec_private_t *) p_codec;
        if (! l_codec->is_decompressor) {
            return l_codec->m_codec_data.m_compression.setup_encoder(	l_codec->m_codec,
                    parameters,
                    p_image,
                    &(l_codec->m_event_mgr) );
        }
    }
    return false;
}
bool OPJ_CALLCONV opj_start_compress (	opj_codec_t *p_codec,
                                        opj_image_t * p_image,
                                        opj_stream_t *p_stream){
    if (p_codec && p_stream) {
        codec_private_t * l_codec = (codec_private_t *) p_codec;
        GrokStream * l_stream = (GrokStream *) p_stream;
        if (! l_codec->is_decompressor) {
            return l_codec->m_codec_data.m_compression.start_compress(	l_codec->m_codec,
                    l_stream,
                    p_image,
                    &(l_codec->m_event_mgr));
        }
    }
    return false;
}
bool OPJ_CALLCONV opj_encode(opj_codec_t *p_info, opj_stream_t *p_stream){
	return opj_encode_with_plugin(p_info, nullptr, p_stream);
}
bool OPJ_CALLCONV opj_encode_with_plugin(opj_codec_t *p_info, 
										grok_plugin_tile_t* tile,
										opj_stream_t *p_stream){
    if (p_info && p_stream) {
        codec_private_t * l_codec = (codec_private_t *) p_info;
        GrokStream * l_stream = (GrokStream *) p_stream;
        if (! l_codec->is_decompressor) {
            return l_codec->m_codec_data.m_compression.encode(	l_codec->m_codec,
					tile,
                    l_stream,
                    &(l_codec->m_event_mgr));
        }
    }
    return false;
}
bool OPJ_CALLCONV opj_end_compress (opj_codec_t *p_codec,
                                    opj_stream_t *p_stream){
    if (p_codec && p_stream) {
        codec_private_t * l_codec = (codec_private_t *) p_codec;
        GrokStream * l_stream = (GrokStream *) p_stream;
        if (! l_codec->is_decompressor) {
            return l_codec->m_codec_data.m_compression.end_compress(l_codec->m_codec,
                    l_stream,
                    &(l_codec->m_event_mgr));
        }
    }
    return false;
}
bool OPJ_CALLCONV opj_end_decompress (	opj_codec_t *p_codec,
                                        opj_stream_t *p_stream){
    if (p_codec && p_stream) {
        codec_private_t * l_codec = (codec_private_t *) p_codec;
        GrokStream * l_stream = (GrokStream *) p_stream;
        if (! l_codec->is_decompressor) {
            return false;
        }
        return l_codec->m_codec_data.m_decompression.end_decompress(l_codec->m_codec,
                l_stream,
                &(l_codec->m_event_mgr) );
    }
    return false;
}

bool OPJ_CALLCONV opj_set_MCT(opj_cparameters_t *parameters,
                              float * pEncodingMatrix,
                              int32_t * p_dc_shift,uint32_t pNbComp){
    uint32_t l_matrix_size = pNbComp * pNbComp * (uint32_t)sizeof(float);
    uint32_t l_dc_shift_size = pNbComp * (uint32_t)sizeof(int32_t);
    uint32_t l_mct_total_size = l_matrix_size + l_dc_shift_size;

    /* add MCT capability */
    if (OPJ_IS_PART2(parameters->rsiz)) {
        parameters->rsiz |= OPJ_EXTENSION_MCT;
    } else {
        parameters->rsiz = ((OPJ_PROFILE_PART2) | (OPJ_EXTENSION_MCT));
    }
    parameters->irreversible = 1;

    /* use array based MCT */
    parameters->tcp_mct = 2;
    parameters->mct_data = grok_malloc(l_mct_total_size);
    if (! parameters->mct_data) {
        return false;
    }
    memcpy(parameters->mct_data,pEncodingMatrix,l_matrix_size);
    memcpy(((uint8_t *) parameters->mct_data) +  l_matrix_size,p_dc_shift,l_dc_shift_size);
    return true;
}
bool OPJ_CALLCONV opj_write_tile (	opj_codec_t *p_codec,
                                    uint32_t p_tile_index,
                                    uint8_t * p_data,
                                    uint64_t p_data_size,
                                    opj_stream_t *p_stream ){
    if (p_codec && p_stream && p_data) {
        codec_private_t * l_codec = (codec_private_t *) p_codec;
        GrokStream * l_stream = (GrokStream *) p_stream;
        if (l_codec->is_decompressor) {
            return false;
        }
        return l_codec->m_codec_data.m_compression.write_tile(	l_codec->m_codec,
                p_tile_index,
                p_data,
                p_data_size,
                l_stream,
                &(l_codec->m_event_mgr) );
    }
    return false;
}

/* ---------------------------------------------------------------------- */

void OPJ_CALLCONV opj_destroy_codec(opj_codec_t *p_codec){
    if (p_codec) {
        codec_private_t * l_codec = (codec_private_t *) p_codec;
        if (l_codec->is_decompressor) {
            l_codec->m_codec_data.m_decompression.destroy(l_codec->m_codec);
        } else {
            l_codec->m_codec_data.m_compression.destroy(l_codec->m_codec);
        }
        l_codec->m_codec = nullptr;
        grok_free(l_codec);
    }
}

/* ---------------------------------------------------------------------- */

void OPJ_CALLCONV opj_dump_codec(	opj_codec_t *p_codec,
                                    int32_t info_flag,
                                    FILE* output_stream){
    if (p_codec) {
        codec_private_t* l_codec = (codec_private_t*) p_codec;
        l_codec->opj_dump_codec(l_codec->m_codec, info_flag, output_stream);
        return;
    }
    /* TODO return error */
    /* fprintf(stderr, "[ERROR] Input parameter of the dump_codec function are incorrect.\n"); */
    return;
}
opj_codestream_info_v2_t* OPJ_CALLCONV opj_get_cstr_info(opj_codec_t *p_codec){
    if (p_codec) {
        codec_private_t* l_codec = (codec_private_t*) p_codec;
        return l_codec->get_codec_info(l_codec->m_codec);
    }
    return nullptr;
}
void OPJ_CALLCONV opj_destroy_cstr_info(opj_codestream_info_v2_t **cstr_info){
    if (cstr_info) {
        if ((*cstr_info)->m_default_tile_info.tccp_info) {
            grok_free((*cstr_info)->m_default_tile_info.tccp_info);
        }

        if ((*cstr_info)->tile_info) {
            /* FIXME not used for the moment*/
        }
        grok_free((*cstr_info));
        (*cstr_info) = nullptr;
    }
}

opj_codestream_index_t * OPJ_CALLCONV opj_get_cstr_index(opj_codec_t *p_codec){
    if (p_codec) {
        codec_private_t* l_codec = (codec_private_t*) p_codec;
        return l_codec->opj_get_codec_index(l_codec->m_codec);
    }
    return nullptr;
}
void OPJ_CALLCONV opj_destroy_cstr_index(opj_codestream_index_t **p_cstr_index){
    if (*p_cstr_index) {
        j2k_destroy_cstr_index(*p_cstr_index);
        (*p_cstr_index) = nullptr;
    }
}

/* ---------------------------------------------------------------------- */

opj_stream_t* OPJ_CALLCONV opj_stream_create_default_file_stream (const char *fname, 
																	bool p_is_read_stream){
    return opj_stream_create_file_stream(fname, stream_chunk_size, p_is_read_stream);
}
opj_stream_t* OPJ_CALLCONV opj_stream_create_file_stream (
    const char *fname,
    size_t p_size,
    bool p_is_read_stream){
	bool stdin_stdout = !fname || !fname[0];
    opj_stream_t* l_stream = nullptr;
    FILE *p_file;
    const char *mode;
	if (!stdin_stdout && (!fname || !fname[0])) {
		return nullptr;
	}
	if (stdin_stdout) {
		p_file = p_is_read_stream ? stdin : stdout;
	}
	else {
		if (p_is_read_stream) mode = "rb";
		else mode = "wb";
		p_file = fopen(fname, mode);
		if (!p_file) {
			return nullptr;
		}
	}
    l_stream = opj_stream_create(p_size,p_is_read_stream);
    if (! l_stream) {
		if (!stdin_stdout)
			fclose(p_file);
        return nullptr;
    }
	opj_stream_set_user_data(l_stream, p_file, (opj_stream_free_user_data_fn) (stdin_stdout ? nullptr : fclose));
	if (p_is_read_stream)
		opj_stream_set_user_data_length(l_stream, opj_get_data_length_from_file(p_file));
    opj_stream_set_read_function(l_stream, (opj_stream_read_fn) grok_read_from_file);
    opj_stream_set_write_function(l_stream, (opj_stream_write_fn) grok_write_from_file);
    opj_stream_set_seek_function(l_stream, (opj_stream_seek_fn) grok_seek_from_file);
    return l_stream;
}
/* ---------------------------------------------------------------------- */
OPJ_API size_t OPJ_CALLCONV opj_stream_get_write_buffer_stream_length(opj_stream_t* stream) {
	if (!stream)
		return 0;
	return get_buffer_stream_offset(stream);
}
opj_stream_t* OPJ_CALLCONV opj_stream_create_buffer_stream(uint8_t *buf,
        size_t len,
		bool ownsBuffer,
        bool p_is_read_stream){
    return create_buffer_stream(buf, len, ownsBuffer,p_is_read_stream);
}
opj_stream_t* OPJ_CALLCONV opj_stream_create_mapped_file_read_stream(const char *fname){
    return create_mapped_file_read_stream(fname);
}
/* ---------------------------------------------------------------------- */
void OPJ_CALLCONV opj_image_all_components_data_free(opj_image_t* image){
    uint32_t i;
    if (!image || !image->comps)
        return;
    for (i = 0; i < image->numcomps; ++i) {
        opj_image_single_component_data_free(image->comps + i);
    }
}
bool OPJ_CALLCONV opj_image_single_component_data_alloc(opj_image_comp_t* comp){
    int32_t* data = nullptr;
    if (!comp)
        return false;
    data = (int32_t*)grok_aligned_malloc(comp->w * comp->h * sizeof(uint32_t));
    if (!data)
        return false;
    opj_image_single_component_data_free(comp);
    comp->data = data;
	comp->owns_data = true;
    return true;
}
void OPJ_CALLCONV opj_image_single_component_data_free(opj_image_comp_t* comp){
    if (!comp || !comp->data || !comp->owns_data)
        return;
    grok_aligned_free(comp->data);
    comp->data = nullptr;
	comp->owns_data = false;
}


/**********************************************************************
Plugin interface implementation
***********************************************************************/


static const char* plugin_get_debug_state_method_name = "plugin_get_debug_state";
static const char* plugin_init_method_name = "plugin_init";
static const char* plugin_encode_method_name = "plugin_encode";
static const char* plugin_batch_encode_method_name = "plugin_batch_encode";
static const char* plugin_stop_batch_encode_method_name = "plugin_stop_batch_encode";
static const char* plugin_is_batch_complete_method_name = "plugin_is_batch_complete";
static const char* plugin_decode_method_name = "plugin_decode";
static const char* plugin_init_batch_decode_method_name = "plugin_init_batch_decode";
static const char* plugin_batch_decode_method_name = "plugin_batch_decode";
static const char* plugin_stop_batch_decode_method_name = "plugin_stop_batch_decode";


static const char* get_path_separator() {
#ifdef _WIN32
	return "\\";
#else
	return "/";
#endif
}

bool pluginLoaded = false;
bool OPJ_CALLCONV grok_plugin_load(grok_plugin_load_info_t info)
{
	// form plugin name
	std::string pluginName = "";
#if !defined(_WIN32)
	pluginName += "lib";
#endif
	pluginName += std::string(GROK_PLUGIN_NAME) + "." + minpf_get_dynamic_library_extension();

	// form absolute plugin path
	auto pluginPath = std::string(info.plugin_path) + get_path_separator() + pluginName;
	int32_t rc = minpf_load_from_path(pluginPath.c_str(), nullptr);

	// if fails, try local path
	if (rc) {
		std::string localPlugin = std::string(".") + get_path_separator() + pluginName;
		rc = minpf_load_from_path(localPlugin.c_str(), nullptr);
	}
	pluginLoaded = !rc;
	if (!pluginLoaded)
		minpf_cleanup_plugin_manager();
	return pluginLoaded;
}
uint32_t OPJ_CALLCONV grok_plugin_get_debug_state(){
    minpf_plugin_manager* mgr = nullptr;
    PLUGIN_GET_DEBUG_STATE func = nullptr;
    uint32_t rc = GROK_PLUGIN_STATE_NO_DEBUG;
    if (!pluginLoaded)
        return rc;
    mgr = minpf_get_plugin_manager();
    if (mgr && mgr->num_libraries > 0) {
        func = (PLUGIN_GET_DEBUG_STATE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_get_debug_state_method_name);
        if (func) {
            rc = func();
        }
    }
    return rc;
}
void OPJ_CALLCONV grok_plugin_cleanup(void){
    minpf_cleanup_plugin_manager();
    pluginLoaded = false;
}
OPJ_API bool OPJ_CALLCONV grok_plugin_init(grok_plugin_init_info_t initInfo) {
	minpf_plugin_manager* mgr = nullptr;
	PLUGIN_INIT func = nullptr;
	if (!pluginLoaded)
		return false;
	mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		func = (PLUGIN_INIT)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_init_method_name);
		if (func) {
			return func(initInfo);
		}
	}
	return false;
}


/*******************
Encode Implementation
********************/

GROK_PLUGIN_ENCODE_USER_CALLBACK userEncodeCallback = 0;

/* wrapper for user's encode callback */
void grok_plugin_internal_encode_callback(plugin_encode_user_callback_info_t* info){
    /* set code block data etc on code object */
    grok_plugin_encode_user_callback_info_t opjInfo;
    memset(&opjInfo, 0, sizeof(grok_plugin_encode_user_callback_info_t));
    opjInfo.input_file_name = info->input_file_name;
	opjInfo.outputFileNameIsRelative = info->outputFileNameIsRelative;
    opjInfo.output_file_name = info->output_file_name;
    opjInfo.encoder_parameters = (opj_cparameters_t*)info->encoder_parameters;
    opjInfo.image = (opj_image_t*)info->image;
    opjInfo.tile = (grok_plugin_tile_t*)info->tile;
    if (userEncodeCallback)
        userEncodeCallback(&opjInfo);
}
int32_t OPJ_CALLCONV grok_plugin_encode(opj_cparameters_t* encode_parameters,
                                       GROK_PLUGIN_ENCODE_USER_CALLBACK callback){
    minpf_plugin_manager* mgr = nullptr;
    PLUGIN_ENCODE func = nullptr;
    if (!pluginLoaded)
        return -1;

    userEncodeCallback = callback;
    mgr = minpf_get_plugin_manager();
    if (mgr && mgr->num_libraries > 0) {
        func = (PLUGIN_ENCODE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_encode_method_name);
        if (func) {
            return func((opj_cparameters_t*)encode_parameters, grok_plugin_internal_encode_callback);
        }
    }
    return -1;
}
int32_t OPJ_CALLCONV grok_plugin_batch_encode(const char* input_dir,
        const char* output_dir,
        opj_cparameters_t* encode_parameters,
        GROK_PLUGIN_ENCODE_USER_CALLBACK callback){
    minpf_plugin_manager* mgr = nullptr;
    PLUGIN_BATCH_ENCODE func = nullptr;
    if (!pluginLoaded)
        return -1;

    userEncodeCallback = callback;
    mgr = minpf_get_plugin_manager();
    if (mgr && mgr->num_libraries > 0) {
        func = (PLUGIN_BATCH_ENCODE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_batch_encode_method_name);
        if (func) {
            return  func(input_dir, output_dir, (opj_cparameters_t*)encode_parameters, grok_plugin_internal_encode_callback);
        }
    }
    return -1;
}

PLUGIN_IS_BATCH_COMPLETE funcPluginIsBatchComplete = nullptr;
OPJ_API bool OPJ_CALLCONV grok_plugin_is_batch_complete(void) {
	minpf_plugin_manager* mgr = nullptr;
	if (!pluginLoaded)
		return true;
	mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		if (!funcPluginIsBatchComplete)
			funcPluginIsBatchComplete = (PLUGIN_IS_BATCH_COMPLETE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_is_batch_complete_method_name);
		if (funcPluginIsBatchComplete) {
			return  funcPluginIsBatchComplete();
		}
	}
	return true;
}
void OPJ_CALLCONV grok_plugin_stop_batch_encode(void){
    minpf_plugin_manager* mgr = nullptr;
    PLUGIN_STOP_BATCH_ENCODE func = nullptr;
    if (!pluginLoaded)
        return;
    mgr = minpf_get_plugin_manager();
    if (mgr && mgr->num_libraries > 0) {
        func = (PLUGIN_STOP_BATCH_ENCODE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_stop_batch_encode_method_name);
        if (func) {
            func();
        }
    }
}

/*******************
Decode Implementation
********************/

grok_plugin_decode_callback decodeCallback = 0;


/* wrapper for user's decode callback */
int32_t grok_plugin_internal_decode_callback(PluginDecodeCallbackInfo* info){
	int32_t rc = -1;
    /* set code block data etc on code object */
    grok_plugin_decode_callback_info_t grokInfo;
    memset(&grokInfo, 0, sizeof(grok_plugin_decode_callback_info_t));
    grokInfo.init_decoders_func  = info->init_decoders_func;
    grokInfo.input_file_name	= info->inputFile.empty() ? nullptr : info->inputFile.c_str();
    grokInfo.output_file_name	= info->outputFile.empty() ? nullptr : info->outputFile.c_str();
	grokInfo.decod_format		= info->decod_format;
	grokInfo.cod_format			= info->cod_format;
    grokInfo.decoder_parameters = info->decoder_parameters;
	grokInfo.l_stream			= info->l_stream;
	grokInfo.l_codec			= info->l_codec;
    grokInfo.image				= info->image;
	grokInfo.plugin_owns_image	= info->plugin_owns_image;
    grokInfo.tile				= info->tile;
	grokInfo.decode_flags = info->decode_flags;
	if (decodeCallback) {
		rc = decodeCallback(&grokInfo);
	}
	//synch
	info->image = grokInfo.image;
	info->l_stream = grokInfo.l_stream;
	info->l_codec = grokInfo.l_codec;
	info->header_info = grokInfo.header_info;
	return rc;
}

int32_t OPJ_CALLCONV grok_plugin_decode(opj_decompress_parameters* decode_parameters,
                                       grok_plugin_decode_callback callback){
    minpf_plugin_manager* mgr = nullptr;
    PLUGIN_DECODE func = nullptr;
    if (!pluginLoaded)
        return -1;

    decodeCallback = callback;
    mgr = minpf_get_plugin_manager();
    func = nullptr;
    if (mgr && mgr->num_libraries > 0) {
        func = (PLUGIN_DECODE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_decode_method_name);
        if (func) {
            return func((opj_decompress_parameters*)decode_parameters, grok_plugin_internal_decode_callback);
        }
    }
    return -1;
}
int32_t OPJ_CALLCONV grok_plugin_init_batch_decode(const char* input_dir,
        const char* output_dir,
        opj_decompress_parameters* decode_parameters,
        grok_plugin_decode_callback callback){
    minpf_plugin_manager* mgr = nullptr;
    PLUGIN_INIT_BATCH_DECODE func = nullptr;
    if (!pluginLoaded)
        return -1;

    decodeCallback = callback;
    mgr = minpf_get_plugin_manager();
    if (mgr && mgr->num_libraries > 0) {
        func = (PLUGIN_INIT_BATCH_DECODE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_init_batch_decode_method_name);
        if (func) {
            return  func(input_dir, output_dir, (opj_decompress_parameters*)decode_parameters, grok_plugin_internal_decode_callback);
        }
    }
    return -1;
}
int32_t OPJ_CALLCONV grok_plugin_batch_decode(void) {
	minpf_plugin_manager* mgr = nullptr;
	PLUGIN_BATCH_DECODE func = nullptr;
	if (!pluginLoaded)
		return -1;
	mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		func = (PLUGIN_BATCH_DECODE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_batch_decode_method_name);
		if (func) {
			return func();
		}
	}
	return -1;
}
void OPJ_CALLCONV grok_plugin_stop_batch_decode(void){
    minpf_plugin_manager* mgr = nullptr;
    PLUGIN_STOP_BATCH_DECODE func = nullptr;
    if (!pluginLoaded)
        return;
    mgr = minpf_get_plugin_manager();
    if (mgr && mgr->num_libraries > 0) {
        func = (PLUGIN_STOP_BATCH_DECODE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_stop_batch_decode_method_name);
        if (func) {
            func();
        }
    }
}