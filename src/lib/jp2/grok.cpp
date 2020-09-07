/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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

#ifdef _WIN32
#include <windows.h>
#else /* _WIN32 */
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#endif
#include <fcntl.h>
#include "grk_includes.h"
using namespace grk;

/**
 * Main codec handler used for compression or decompression.
 */
struct grk_codec_private {
	ICodeStream *m_codeStreamBase;
	 grk_stream  *m_stream;
	/** Flag to indicate if the codec is used to decompress or compress*/
	bool is_decompressor;
};

ThreadPool* ThreadPool::singleton = nullptr;
std::mutex ThreadPool::singleton_mutex;

static bool is_plugin_initialized = false;
bool GRK_CALLCONV grk_initialize(const char *plugin_path, uint32_t numthreads) {
	ThreadPool::instance(numthreads);
	if (!is_plugin_initialized) {
		grk_plugin_load_info info;
		info.plugin_path = plugin_path;
		is_plugin_initialized = grk_plugin_load(info);
	}
	return is_plugin_initialized;
}

GRK_API void GRK_CALLCONV grk_deinitialize() {
	grk_plugin_cleanup();
	ThreadPool::release();
}

/* ---------------------------------------------------------------------- */
/* Functions to set the message handlers */

bool GRK_CALLCONV grk_set_info_handler( grk_msg_callback p_callback,
		void *p_user_data) {
	logger::m_logger.info_handler = p_callback;
	logger::m_logger.m_info_data = p_user_data;

	return true;
}
bool GRK_CALLCONV grk_set_warning_handler( 	grk_msg_callback p_callback,
		void *p_user_data) {
	logger::m_logger.warning_handler = p_callback;
	logger::m_logger.m_warning_data = p_user_data;
	return true;
}
bool GRK_CALLCONV grk_set_error_handler( grk_msg_callback p_callback,
		void *p_user_data) {
	logger::m_logger.error_handler = p_callback;
	logger::m_logger.m_error_data = p_user_data;
	return true;
}
/* ---------------------------------------------------------------------- */

static size_t grk_read_from_file(void *p_buffer, size_t nb_bytes,
		FILE *p_file) {
	return fread(p_buffer, 1, nb_bytes, p_file);
}

static uint64_t grk_get_data_length_from_file(FILE *p_file) {
	GROK_FSEEK(p_file, 0, SEEK_END);
	int64_t file_length = (int64_t) GROK_FTELL(p_file);
	GROK_FSEEK(p_file, 0, SEEK_SET);
	return (uint64_t) file_length;
}
static size_t grk_write_to_file(void *p_buffer, size_t nb_bytes,
		FILE *p_file) {
	return fwrite(p_buffer, 1, nb_bytes, p_file);
}
static bool grk_seek_in_file(int64_t nb_bytes, FILE *p_user_data) {
	return GROK_FSEEK(p_user_data, nb_bytes, SEEK_SET) ? false : true;
}

/* ---------------------------------------------------------------------- */

/* ---------------------------------------------------------------------- */

#ifdef _WIN32
#ifndef GRK_STATIC
BOOL APIENTRY
DllMain(HINSTANCE hModule, DWORD ul_reason_for_call, LPVOID lpReserved){
    GRK_UNUSED(lpReserved);
    GRK_UNUSED(hModule);
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
#endif /* GRK_STATIC */
#endif /* _WIN32 */

/* ---------------------------------------------------------------------- */

const char* GRK_CALLCONV grk_version(void) {
	return GRK_PACKAGE_VERSION;
}


grk_image *  GRK_CALLCONV grk_image_create(uint32_t numcmpts,
		 grk_image_cmptparm  *cmptparms, GRK_COLOR_SPACE clrspc, bool allocData) {
	auto image = (grk_image * ) grk::grk_calloc(1, sizeof(grk_image));

	if (image) {
		image->color_space = clrspc;
		image->numcomps = numcmpts;
		/* allocate memory for the per-component information */
		image->comps = ( grk_image_comp  * ) grk::grk_calloc(1,
				image->numcomps * sizeof( grk_image_comp) );
		if (!image->comps) {
			grk::GRK_ERROR("Unable to allocate memory for image.");
			grk_image_destroy(image);
			return nullptr;
		}
		/* create the individual image components */
		for (uint32_t compno = 0; compno < numcmpts; compno++) {
			auto comp = &image->comps[compno];

			assert(cmptparms[compno].dx);
			assert(cmptparms[compno].dy);
			comp->dx = cmptparms[compno].dx;
			comp->dy = cmptparms[compno].dy;
			comp->w = cmptparms[compno].w;
			comp->h = cmptparms[compno].h;
			comp->x0 = cmptparms[compno].x0;
			comp->y0 = cmptparms[compno].y0;
			comp->prec = cmptparms[compno].prec;
			comp->sgnd = cmptparms[compno].sgnd;
			if (allocData && !grk::grk_image_single_component_data_alloc(comp)) {
				grk::GRK_ERROR("Unable to allocate memory for image.");
				grk_image_destroy(image);
				return nullptr;
			}
			comp->type = GRK_COMPONENT_TYPE_COLOUR;
			switch(compno){
			case 0:
				comp->association = GRK_COMPONENT_ASSOC_COLOUR_1;
				break;
			case 1:
				comp->association = GRK_COMPONENT_ASSOC_COLOUR_2;
				break;
			case 2:
				comp->association = GRK_COMPONENT_ASSOC_COLOUR_3;
				break;
			default:
				comp->association = GRK_COMPONENT_ASSOC_UNASSOCIATED;
				comp->type = GRK_COMPONENT_TYPE_UNSPECIFIED;
				break;
			}
		}
	}

	return image;
}

void GRK_CALLCONV grk_image_destroy(grk_image *image) {
	if (image) {
		if (image->comps) {
			grk_image_all_components_data_free(image);
			grk::grk_free(image->comps);
		}
		delete[] image->icc_profile_buf;
		delete[] image->iptc_buf;
		delete[] image->xmp_buf;
		grk::grk_free(image);
	}
}



/* ---------------------------------------------------------------------- */
/* DECOMPRESSION FUNCTIONS*/

 grk_codec   GRK_CALLCONV grk_create_decompress(GRK_CODEC_FORMAT p_format,
		 grk_stream  *stream) {
	auto codec =
			(grk_codec_private*) grk_calloc(1, sizeof(grk_codec_private));
	if (!codec) {
		return nullptr;
	}
	codec->is_decompressor = 1;
	codec->m_stream = stream;

	switch (p_format) {
	case GRK_CODEC_J2K:
		codec->m_codeStreamBase = new CodeStream(true,(BufferedStream*)stream);
		break;
	case GRK_CODEC_JP2:
		codec->m_codeStreamBase = new FileFormat(true,(BufferedStream*)stream);
		break;
	case GRK_CODEC_UNKNOWN:
	default:
		grk_free(codec);
		return nullptr;
	}
	return ( grk_codec  ) codec;
}
void GRK_CALLCONV grk_set_default_decompress_params(
		 grk_dparameters  *parameters) {
	if (parameters) {
		memset(parameters, 0, sizeof( grk_dparameters) );
	}
}
bool GRK_CALLCONV grk_init_decompress( grk_codec p_codec,
		 grk_dparameters  *parameters) {
	if (p_codec && parameters) {
		auto codec = (grk_codec_private*) p_codec;
		assert(codec->is_decompressor);
		codec->m_codeStreamBase->init_decompress(parameters);
		return true;
	}
	return false;
}
bool GRK_CALLCONV grk_read_header(
		 grk_codec p_codec,  grk_header_info  *header_info,
		grk_image **p_image) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		assert(codec->is_decompressor);
		return codec->m_codeStreamBase->read_header( header_info, p_image);
	}
	return false;
}

bool GRK_CALLCONV grk_decompress( grk_codec p_codec, grk_plugin_tile *tile,
		 grk_image *p_image) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		assert(codec->is_decompressor);

		return codec->m_codeStreamBase->decompress(tile, p_image);
	}
	return false;
}
bool GRK_CALLCONV grk_set_decompress_area( grk_codec p_codec,
		grk_image *p_image, uint32_t start_x, uint32_t start_y,
		uint32_t end_x, uint32_t end_y) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		assert(codec->is_decompressor);
		return codec->m_codeStreamBase->set_decompress_area(p_image, start_x, start_y, end_x,
				end_y);
	}
	return false;
}
bool GRK_CALLCONV grk_decompress_tile( grk_codec p_codec,
		 grk_image *p_image, uint16_t tile_index) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		assert(codec->is_decompressor);

		return codec->m_codeStreamBase->decompress_tile(p_image,tile_index);
	}
	return false;
}

/* ---------------------------------------------------------------------- */
/* COMPRESSION FUNCTIONS*/

 grk_codec   GRK_CALLCONV grk_create_compress(GRK_CODEC_FORMAT p_format,
		 grk_stream  *stream) {
	auto codec =
			(grk_codec_private*) grk_calloc(1, sizeof(grk_codec_private));
	if (!codec) {
		return nullptr;
	}
	codec->m_stream = stream;
	codec->is_decompressor = 0;

	switch (p_format) {
	case GRK_CODEC_J2K:
		codec->m_codeStreamBase = new CodeStream(false,(BufferedStream*)stream);
		break;
	case GRK_CODEC_JP2:
		codec->m_codeStreamBase = new FileFormat(false,(BufferedStream*)stream);
		break;
	case GRK_CODEC_UNKNOWN:
	default:
		grk_free(codec);
		return nullptr;
	}
	return ( grk_codec  ) codec;
}
void GRK_CALLCONV grk_set_default_compress_params(
		 grk_cparameters  *parameters) {
	if (parameters) {
		memset(parameters, 0, sizeof( grk_cparameters) );
		/* default coding parameters */
		parameters->rsiz = GRK_PROFILE_NONE;
		parameters->max_comp_size = 0;
		parameters->numresolution = GRK_COMP_PARAM_DEFAULT_NUMRESOLUTION;
		parameters->cblockw_init = GRK_COMP_PARAM_DEFAULT_CBLOCKW;
		parameters->cblockh_init = GRK_COMP_PARAM_DEFAULT_CBLOCKH;
		parameters->prog_order = GRK_COMP_PARAM_DEFAULT_PROG_ORDER;
		parameters->roi_compno = -1; /* no ROI */
		parameters->subsampling_dx = 1;
		parameters->subsampling_dy = 1;
		parameters->tp_on = 0;
		parameters->decod_format = GRK_UNK_FMT;
		parameters->cod_format = GRK_UNK_FMT;
		parameters->tcp_rates[0] = 0;
		parameters->tcp_numlayers = 0;
		parameters->cp_disto_alloc = false;
		parameters->cp_fixed_quality = false;
		parameters->writePLT = false;
		parameters->writeTLM = false;
		if (!parameters->numThreads)
			parameters->numThreads = ThreadPool::hardware_concurrency();
		parameters->deviceId = 0;
		parameters->repeats = 1;
	}
}
bool GRK_CALLCONV grk_init_compress( grk_codec p_codec,
		 grk_cparameters  *parameters, grk_image *p_image) {
	if (p_codec && parameters && p_image) {
		auto codec = (grk_codec_private*) p_codec;
		assert(!codec->is_decompressor);
		if (!codec->is_decompressor) {
			return codec->m_codeStreamBase->init_compress(parameters, p_image);
		}
	}
	return false;
}
bool GRK_CALLCONV grk_start_compress( grk_codec p_codec) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		assert(!codec->is_decompressor);
		if (!codec->is_decompressor) {
			return codec->m_codeStreamBase->start_compress();
		}
	}
	return false;
}
bool GRK_CALLCONV grk_compress( grk_codec p_codec) {
	return grk_compress_with_plugin(p_codec, nullptr);
}
bool GRK_CALLCONV grk_compress_with_plugin( grk_codec p_info,
		grk_plugin_tile *tile) {
	if (p_info) {
		auto codec = (grk_codec_private*) p_info;
		assert(!codec->is_decompressor);
		if (!codec->is_decompressor) {
			return codec->m_codeStreamBase->compress(tile);
		}
	}
	return false;
}
bool GRK_CALLCONV grk_end_compress( grk_codec p_codec) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		assert(!codec->is_decompressor);
		if (!codec->is_decompressor) {
			return codec->m_codeStreamBase->end_compress();
		}
	}
	return false;
}
bool GRK_CALLCONV grk_end_decompress( grk_codec p_codec) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		assert(codec->is_decompressor);
		return codec->m_codeStreamBase->end_decompress();
	}
	return false;
}

bool GRK_CALLCONV grk_set_MCT( grk_cparameters  *parameters,
		float *pEncodingMatrix, int32_t *p_dc_shift, uint32_t pNbComp) {
	uint32_t l_matrix_size = pNbComp * pNbComp * (uint32_t) sizeof(float);
	uint32_t l_dc_shift_size = pNbComp * (uint32_t) sizeof(int32_t);
	uint32_t l_mct_total_size = l_matrix_size + l_dc_shift_size;

	/* add MCT capability */
	if (GRK_IS_PART2(parameters->rsiz)) {
		parameters->rsiz |= GRK_EXTENSION_MCT;
	} else {
		parameters->rsiz = ((GRK_PROFILE_PART2) | (GRK_EXTENSION_MCT));
	}
	parameters->irreversible = true;

	/* use array based MCT */
	parameters->tcp_mct = 2;
	parameters->mct_data = grk_malloc(l_mct_total_size);
	if (!parameters->mct_data) {
		return false;
	}
	memcpy(parameters->mct_data, pEncodingMatrix, l_matrix_size);
	memcpy(((uint8_t*) parameters->mct_data) + l_matrix_size, p_dc_shift,
			l_dc_shift_size);
	return true;
}
bool GRK_CALLCONV grk_compress_tile( grk_codec p_codec, uint16_t tile_index,
		uint8_t *p_data, uint64_t data_size) {
	if (p_codec && p_data) {
		auto codec = (grk_codec_private*) p_codec;
		if (codec->is_decompressor)
			return false;
		return codec->m_codeStreamBase->compress_tile(tile_index, p_data, data_size	);
	}
	return false;
}

/* ---------------------------------------------------------------------- */

void GRK_CALLCONV grk_destroy_codec( grk_codec p_codec) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		delete codec->m_codeStreamBase;
		codec->m_codeStreamBase = nullptr;
		grk_free(codec);
	}
}

/* ---------------------------------------------------------------------- */

void GRK_CALLCONV grk_dump_codec( grk_codec p_codec, uint32_t info_flag,
		FILE *output_stream) {
	assert(p_codec);
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		codec->m_codeStreamBase->dump(info_flag, output_stream);
	}
}
 grk_codestream_info_v2  *  GRK_CALLCONV grk_get_cstr_info( grk_codec p_codec) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		return codec->m_codeStreamBase->get_cstr_info();
	}
	return nullptr;
}
void GRK_CALLCONV grk_destroy_cstr_info( grk_codestream_info_v2  **cstr_info) {
	if (cstr_info) {
		if ((*cstr_info)->m_default_tile_info.tccp_info)
			grk_free((*cstr_info)->m_default_tile_info.tccp_info);
		grk_free((*cstr_info));
		(*cstr_info) = nullptr;
	}
}

 grk_codestream_index  *  GRK_CALLCONV grk_get_cstr_index( grk_codec p_codec) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		return codec->m_codeStreamBase->get_cstr_index();
	}
	return nullptr;
}
void GRK_CALLCONV grk_destroy_cstr_index(
		 grk_codestream_index  **p_cstr_index) {
	if (*p_cstr_index) {
		j2k_destroy_cstr_index(*p_cstr_index);
		(*p_cstr_index) = nullptr;
	}
}

/* ---------------------------------------------------------------------- */

static void grk_free_file(void *p_user_data) {
	if (p_user_data)
		fclose((FILE*) p_user_data);
}

 grk_stream  *  GRK_CALLCONV grk_stream_create_file_stream(const char *fname,
		size_t p_size, bool is_read_stream) {
	bool stdin_stdout = !fname || !fname[0];
	FILE *p_file = nullptr;
	if (!stdin_stdout && (!fname || !fname[0]))
		return nullptr;
	if (stdin_stdout) {
		p_file = is_read_stream ? stdin : stdout;
	} else {
		const char *mode = (is_read_stream) ? "rb" : "wb";
		p_file = fopen(fname, mode);
		if (!p_file) {
			return nullptr;
		}
	}
	auto stream = grk_stream_create(p_size, is_read_stream);
	if (!stream) {
		if (!stdin_stdout)
			fclose(p_file);
		return nullptr;
	}
	grk_stream_set_user_data(stream, (void*) p_file,
			(grk_stream_free_user_data_fn) (
					stdin_stdout ? nullptr : grk_free_file));
	if (is_read_stream)
		grk_stream_set_user_data_length(stream,
				grk_get_data_length_from_file(p_file));
	grk_stream_set_read_function(stream,
			(grk_stream_read_fn) grk_read_from_file);
	grk_stream_set_write_function(stream,
			(grk_stream_write_fn) grk_write_to_file);
	grk_stream_set_seek_function(stream,
			(grk_stream_seek_fn) grk_seek_in_file);
	return stream;
}
/* ---------------------------------------------------------------------- */
GRK_API size_t GRK_CALLCONV grk_stream_get_write_mem_stream_length(
		 grk_stream  *stream) {
	if (!stream)
		return 0;
	return get_mem_stream_offset(stream);
}
 grk_stream  *  GRK_CALLCONV grk_stream_create_mem_stream(uint8_t *buf,
		size_t len, bool ownsBuffer, bool is_read_stream) {
	return create_mem_stream(buf, len, ownsBuffer, is_read_stream);
}
 grk_stream  *  GRK_CALLCONV grk_stream_create_mapped_file_stream(
		const char *fname, bool read_stream) {
	 if (read_stream)
		 return create_mapped_file_read_stream(fname);
	 else
		 return create_mapped_file_write_stream(fname);
}
/* ---------------------------------------------------------------------- */
void GRK_CALLCONV grk_image_all_components_data_free(grk_image *image) {
	uint32_t i;
	if (!image || !image->comps)
		return;
	for (i = 0; i < image->numcomps; ++i)
		grk_image_single_component_data_free(image->comps + i);
}

void GRK_CALLCONV grk_image_single_component_data_free( grk_image_comp  *comp) {
	if (!comp || !comp->data || !comp->owns_data)
		return;
	grk_aligned_free(comp->data);
	comp->data = nullptr;
	comp->owns_data = false;
}

/**********************************************************************
 Plugin interface implementation
 ***********************************************************************/

static const char *plugin_get_debug_state_method_name = "plugin_get_debug_state";
static const char *plugin_init_method_name = "plugin_init";
static const char *plugin_encode_method_name = "plugin_encode";
static const char *plugin_batch_encode_method_name = "plugin_batch_encode";
static const char *plugin_stop_batch_encode_method_name =
		"plugin_stop_batch_encode";
static const char *plugin_is_batch_complete_method_name =
		"plugin_is_batch_complete";
static const char *plugin_decode_method_name = "plugin_decode";
static const char *plugin_init_batch_decode_method_name =
		"plugin_init_batch_decode";
static const char *plugin_batch_decode_method_name = "plugin_batch_decode";
static const char *plugin_stop_batch_decode_method_name =
		"plugin_stop_batch_decode";

static const char* get_path_separator() {
#ifdef _WIN32
	return "\\";
#else
	return "/";
#endif
}

bool pluginLoaded = false;
bool GRK_CALLCONV grk_plugin_load(grk_plugin_load_info info) {
	if (!info.plugin_path)
		return false;

	// form plugin name
	std::string pluginName = "";
#if !defined(_WIN32)
	pluginName += "lib";
#endif
	pluginName += std::string(GROK_PLUGIN_NAME) + "."
			+ minpf_get_dynamic_library_extension();

	// form absolute plugin path
	auto pluginPath = std::string(info.plugin_path) + get_path_separator()
			+ pluginName;
	int32_t rc = minpf_load_from_path(pluginPath.c_str(), nullptr);

	// if fails, try local path
	if (rc) {
		std::string localPlugin = std::string(".") + get_path_separator()
				+ pluginName;
		rc = minpf_load_from_path(localPlugin.c_str(), nullptr);
	}
	pluginLoaded = !rc;
	if (!pluginLoaded)
		minpf_cleanup_plugin_manager();
	return pluginLoaded;
}
uint32_t GRK_CALLCONV grk_plugin_get_debug_state() {
	uint32_t rc = GRK_PLUGIN_STATE_NO_DEBUG;
	if (!pluginLoaded)
		return rc;
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_GET_DEBUG_STATE) minpf_get_symbol(
				mgr->dynamic_libraries[0], plugin_get_debug_state_method_name);
		if (func)
			rc = func();
	}
	return rc;
}
void GRK_CALLCONV grk_plugin_cleanup(void) {
	minpf_cleanup_plugin_manager();
	pluginLoaded = false;
}
GRK_API bool GRK_CALLCONV grk_plugin_init(grk_plugin_init_info initInfo) {
	if (!pluginLoaded)
		return false;
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_INIT) minpf_get_symbol(mgr->dynamic_libraries[0],
				plugin_init_method_name);
		if (func)
			return func(initInfo);
	}
	return false;
}

/*******************
 Encode Implementation
 ********************/

GRK_PLUGIN_ENCODE_USER_CALLBACK userEncodeCallback = 0;

/* wrapper for user's compress callback */
void grk_plugin_internal_encode_callback(
		plugin_encode_user_callback_info *info) {
	/* set code block data etc on code object */
	grk_plugin_encode_user_callback_info grk_info;
	memset(&grk_info, 0, sizeof(grk_plugin_encode_user_callback_info));
	grk_info.input_file_name = info->input_file_name;
	grk_info.outputFileNameIsRelative = info->outputFileNameIsRelative;
	grk_info.output_file_name = info->output_file_name;
	grk_info.encoder_parameters = ( grk_cparameters  * ) info->encoder_parameters;
	grk_info.image = (grk_image * ) info->image;
	grk_info.tile = (grk_plugin_tile*) info->tile;
	if (userEncodeCallback)
		userEncodeCallback(&grk_info);
}
int32_t GRK_CALLCONV grk_plugin_compress( grk_cparameters  *encode_parameters,
		GRK_PLUGIN_ENCODE_USER_CALLBACK callback) {
	if (!pluginLoaded)
		return -1;
	userEncodeCallback = callback;
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_ENCODE) minpf_get_symbol(mgr->dynamic_libraries[0],
				plugin_encode_method_name);
		if (func)
			return func(( grk_cparameters  * ) encode_parameters,
					grk_plugin_internal_encode_callback);
	}
	return -1;
}
int32_t GRK_CALLCONV grk_plugin_batch_compress(const char *input_dir,
		const char *output_dir,  grk_cparameters  *encode_parameters,
		GRK_PLUGIN_ENCODE_USER_CALLBACK callback) {
	if (!pluginLoaded)
		return -1;
	userEncodeCallback = callback;
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_BATCH_ENCODE) minpf_get_symbol(mgr->dynamic_libraries[0],
				plugin_batch_encode_method_name);
		if (func) {
			return func(input_dir, output_dir,
					( grk_cparameters  * ) encode_parameters,
					grk_plugin_internal_encode_callback);
		}
	}
	return -1;
}

PLUGIN_IS_BATCH_COMPLETE funcPluginIsBatchComplete = nullptr;
GRK_API bool GRK_CALLCONV grk_plugin_is_batch_complete(void) {
	if (!pluginLoaded)
		return true;
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		if (!funcPluginIsBatchComplete)
			funcPluginIsBatchComplete =
					(PLUGIN_IS_BATCH_COMPLETE) minpf_get_symbol(
							mgr->dynamic_libraries[0],
							plugin_is_batch_complete_method_name);
		if (funcPluginIsBatchComplete)
			return funcPluginIsBatchComplete();
	}
	return true;
}
void GRK_CALLCONV grk_plugin_stop_batch_compress(void) {
	if (!pluginLoaded)
		return;
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_STOP_BATCH_ENCODE) minpf_get_symbol(
				mgr->dynamic_libraries[0],
				plugin_stop_batch_encode_method_name);
		if (func)
			func();
	}
}

/*******************
 Decode Implementation
 ********************/

grk_plugin_decode_callback decodeCallback = 0;

/* wrapper for user's decompress callback */
int32_t grk_plugin_internal_decode_callback(PluginDecodeCallbackInfo *info) {
	int32_t rc = -1;
	/* set code block data etc on code object */
	grk_plugin_decode_callback_info grokInfo;
	memset(&grokInfo, 0, sizeof(grk_plugin_decode_callback_info));
	grokInfo.init_decoders_func = info->init_decoders_func;
	grokInfo.input_file_name =
			info->inputFile.empty() ? nullptr : info->inputFile.c_str();
	grokInfo.output_file_name =
			info->outputFile.empty() ? nullptr : info->outputFile.c_str();
	grokInfo.decod_format = info->decod_format;
	grokInfo.cod_format = info->cod_format;
	grokInfo.decoder_parameters = info->decoder_parameters;
	grokInfo.l_stream = info->l_stream;
	grokInfo.l_codec = info->l_codec;
	grokInfo.image = info->image;
	grokInfo.plugin_owns_image = info->plugin_owns_image;
	grokInfo.tile = info->tile;
	grokInfo.decode_flags = info->decode_flags;
	if (decodeCallback)
		rc = decodeCallback(&grokInfo);
	//synch
	info->image = grokInfo.image;
	info->l_stream = grokInfo.l_stream;
	info->l_codec = grokInfo.l_codec;
	info->header_info = grokInfo.header_info;
	return rc;
}

int32_t GRK_CALLCONV grk_plugin_decompress(
		grk_decompress_parameters *decode_parameters,
		grk_plugin_decode_callback callback) {
	if (!pluginLoaded)
		return -1;
	decodeCallback = callback;
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_DECODE) minpf_get_symbol(mgr->dynamic_libraries[0],
				plugin_decode_method_name);
		if (func)
			return func((grk_decompress_parameters*) decode_parameters,
					grk_plugin_internal_decode_callback);
	}
	return -1;
}
int32_t GRK_CALLCONV grk_plugin_init_batch_decompress(const char *input_dir,
		const char *output_dir, grk_decompress_parameters *decode_parameters,
		grk_plugin_decode_callback callback) {
	if (!pluginLoaded)
		return -1;
	decodeCallback = callback;
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_INIT_BATCH_DECODE) minpf_get_symbol(
				mgr->dynamic_libraries[0],
				plugin_init_batch_decode_method_name);
		if (func)
			return func(input_dir, output_dir,
					(grk_decompress_parameters*) decode_parameters,
					grk_plugin_internal_decode_callback);
	}
	return -1;
}
int32_t GRK_CALLCONV grk_plugin_batch_decompress(void) {
	if (!pluginLoaded)
		return -1;
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_BATCH_DECODE) minpf_get_symbol(mgr->dynamic_libraries[0],
				plugin_batch_decode_method_name);
		if (func)
			return func();
	}
	return -1;
}
void GRK_CALLCONV grk_plugin_stop_batch_decompress(void) {
	if (!pluginLoaded)
		return;
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_STOP_BATCH_DECODE) minpf_get_symbol(
				mgr->dynamic_libraries[0],
				plugin_stop_batch_decode_method_name);
		if (func)
			func();
	}
}
