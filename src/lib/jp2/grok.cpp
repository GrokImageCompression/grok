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
#include "grok_includes.h"
using namespace grk;

/**
 * Main codec handler used for compression or decompression.
 */
struct grk_codec_private {
	union {
		/**
		 * Decompression handler.
		 */
		struct decompression {
			/** Main header reading function handler */
			bool (*read_header)(BufferedStream *stream, void *p_codec,
					 grk_header_info  *header_info, grk_image **p_image);

			/** Decoding function */
			bool (*decompress)(void *p_codec, grk_plugin_tile *tile,
					BufferedStream *stream, grk_image *p_image);

			/** Read tile header */
			bool (*read_tile_header)(void *p_codec, uint16_t *tile_index,
					bool *p_should_go_on, BufferedStream *stream);


			/** Reading function used after code stream if necessary */
			bool (*end_decompress)(void *p_codec, BufferedStream *stream);

			/** Codec destroy function handler */
			void (*destroy)(void *p_codec);

			/** Setup decoder function handler */
			void (*setup_decoder)(void *p_codec,  grk_dparameters  *p_param);

			/** Set decompress area function handler */
			bool (*set_decompress_area)(void *p_codec, grk_image *p_image,
					uint32_t start_x, uint32_t end_x, uint32_t start_y,
					uint32_t end_y);

			/** decompress tile*/
			bool (*decompress_tile)(void *p_codec, BufferedStream *stream,
					grk_image *p_image,
					uint16_t tile_index);

		} m_decompression;

		/**
		 * Compression handler
		 */
		struct compression {
			bool (*start_compress)(void *p_codec, BufferedStream *stream);

			bool (*compress)(void *p_codec, grk_plugin_tile*,
					BufferedStream *stream);

			bool (*write_tile)(void *p_codec, TileProcessor *tileProcessor,
					uint16_t tile_index, uint8_t *p_data, uint64_t data_size, BufferedStream *stream);

			bool (*end_compress)(void *p_codec, BufferedStream *stream);

			void (*destroy)(void *p_codec);

			bool (*init_compress)(void *p_codec,  grk_cparameters  *p_param,
					grk_image *p_image);
		} m_compression;
	} m_codec_data;
	/** opaque code struct*/
	void *m_codec;
	 grk_stream  *m_stream;
	/** Flag to indicate if the codec is used to decompress or compress*/
	bool is_decompressor;
	void (*grk_dump_codec)(void *p_codec, uint32_t info_flag,
			FILE *output_stream);
	 grk_codestream_info_v2  *  (*get_codec_info)(void *p_codec);
	 grk_codestream_index  *  (*grk_get_codec_index)(void *p_codec);
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
static size_t grk_write_from_file(void *p_buffer, size_t nb_bytes,
		FILE *p_file) {
	return fwrite(p_buffer, 1, nb_bytes, p_file);
}
static bool grok_seek_from_file(int64_t nb_bytes, FILE *p_user_data) {
	return GROK_FSEEK(p_user_data, nb_bytes, SEEK_SET) ? false : true;
}

/* ---------------------------------------------------------------------- */

/* ---------------------------------------------------------------------- */

#ifdef _WIN32
#ifndef GRK_STATIC
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
#endif /* GRK_STATIC */
#endif /* _WIN32 */

/* ---------------------------------------------------------------------- */

const char* GRK_CALLCONV grk_version(void) {
	return GRK_PACKAGE_VERSION;
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
		codec->grk_dump_codec = (void (*)(void*, uint32_t, FILE*)) j2k_dump;

		codec->get_codec_info =
				( grk_codestream_info_v2  *  (*)(void*)) j2k_get_cstr_info;

		codec->grk_get_codec_index =
				( grk_codestream_index  *  (*)(void*)) j2k_get_cstr_index;

		codec->m_codec_data.m_decompression.decompress =
				(bool (*)(void*, grk_plugin_tile*, BufferedStream*, grk_image * )) j2k_decompress;

		codec->m_codec_data.m_decompression.end_decompress = (bool (*)(void*,
				BufferedStream*)) j2k_end_decompress;

		codec->m_codec_data.m_decompression.read_header = (bool (*)(
				BufferedStream*, void*,  grk_header_info  *header_info,
				grk_image **)) j2k_read_header;

		codec->m_codec_data.m_decompression.destroy =
				(void (*)(void*)) j2k_destroy;

		codec->m_codec_data.m_decompression.setup_decoder = (void (*)(void*,
				 grk_dparameters  * )) j2k_init_decompressor;

		codec->m_codec_data.m_decompression.read_tile_header =
				(bool (*)(void*, uint16_t*, bool*, BufferedStream*)) j2k_read_tile_header;


		codec->m_codec_data.m_decompression.set_decompress_area = (bool (*)(void*,
				grk_image * , uint32_t, uint32_t, uint32_t, uint32_t)) j2k_set_decompress_area;

		codec->m_codec_data.m_decompression.decompress_tile = (bool (*)(
				void *p_codec, BufferedStream *stream, grk_image *p_image, uint16_t tile_index)) j2k_decompress_tile;

		codec->m_codec = new CodeStream(true);

		if (!codec->m_codec) {
			grk_free(codec);
			return nullptr;
		}
		break;
	case GRK_CODEC_JP2:
		/* get a JP2 decoder handle */
		codec->grk_dump_codec = (void (*)(void*, uint32_t, FILE*)) jp2_dump;
		codec->get_codec_info =
				( grk_codestream_info_v2  *  (*)(void*)) jp2_get_cstr_info;
		codec->grk_get_codec_index =
				( grk_codestream_index  *  (*)(void*)) jp2_get_cstr_index;
		codec->m_codec_data.m_decompression.decompress =
				(bool (*)(void*, grk_plugin_tile*, BufferedStream*, grk_image * )) jp2_decompress;
		codec->m_codec_data.m_decompression.end_decompress = (bool (*)(void*,
				BufferedStream*)) jp2_end_decompress;
		codec->m_codec_data.m_decompression.read_header = (bool (*)(
				BufferedStream*, void*,  grk_header_info  *header_info,
				grk_image **)) jp2_read_header;
		codec->m_codec_data.m_decompression.read_tile_header =
				(bool (*)(void*, uint16_t*, bool*, BufferedStream*)) jp2_read_tile_header;

		codec->m_codec_data.m_decompression.destroy =
				(void (*)(void*)) jp2_destroy;
		codec->m_codec_data.m_decompression.setup_decoder = (void (*)(void*,
				 grk_dparameters  * )) jp2_init_decompress;
		codec->m_codec_data.m_decompression.set_decompress_area = (bool (*)(void*,
				grk_image * , uint32_t, uint32_t, uint32_t, uint32_t)) jp2_set_decompress_area;

		codec->m_codec_data.m_decompression.decompress_tile = (bool (*)(
				void *p_codec, BufferedStream *stream, grk_image *p_image, uint16_t tile_index)) jp2_decompress_tile;
		codec->m_codec = new FileFormat(true);
		if (!codec->m_codec) {
			grk_free(codec);
			return nullptr;
		}
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
		if (!codec->is_decompressor) {
			GROK_ERROR(
					"Codec provided to the grk_init_decompress function is not a decompressor handler.");
			return false;
		}
		codec->m_codec_data.m_decompression.setup_decoder(codec->m_codec,
				parameters);
		return true;
	}
	return false;
}
bool GRK_CALLCONV grk_read_header(
		 grk_codec p_codec,  grk_header_info  *header_info,
		grk_image **p_image) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		auto stream = (BufferedStream*) codec->m_stream;
		if (!codec->is_decompressor) {
			GROK_ERROR(
					"Codec provided to the grk_read_header function is not a decompressor handler.");
			return false;
		}

		return codec->m_codec_data.m_decompression.read_header(stream,
				codec->m_codec, header_info, p_image);
	}
	return false;
}

bool GRK_CALLCONV grk_decompress( grk_codec p_codec, grk_plugin_tile *tile,
		 grk_image *p_image) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		auto stream = (BufferedStream*) codec->m_stream;
		if (!codec->is_decompressor)
			return false;
		return codec->m_codec_data.m_decompression.decompress(codec->m_codec,
				tile, stream, p_image);
	}
	return false;
}
bool GRK_CALLCONV grk_set_decompress_area( grk_codec p_codec,
		grk_image *p_image, uint32_t start_x, uint32_t start_y,
		uint32_t end_x, uint32_t end_y) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		if (!codec->is_decompressor)
			return false;
		return codec->m_codec_data.m_decompression.set_decompress_area(
				codec->m_codec, p_image, start_x, start_y, end_x,
				end_y);
	}
	return false;
}
bool GRK_CALLCONV grk_decompress_tile( grk_codec p_codec,
		 grk_image *p_image, uint16_t tile_index) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		auto stream = (BufferedStream*) codec->m_stream;

		if (!codec->is_decompressor) {
			return false;
		}

		return codec->m_codec_data.m_decompression.decompress_tile(
				codec->m_codec, stream, p_image,
				tile_index);
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
		codec->m_codec_data.m_compression.compress =
				(bool (*)(void*, grk_plugin_tile*, BufferedStream*)) j2k_compress;
		codec->m_codec_data.m_compression.end_compress = (bool (*)(void*,
				BufferedStream*)) j2k_end_compress;
		codec->m_codec_data.m_compression.start_compress =
				(bool (*)(void*, BufferedStream*)) j2k_start_compress;
		codec->m_codec_data.m_compression.write_tile =
				(bool (*)(void*, TileProcessor*, uint16_t, uint8_t*, uint64_t, BufferedStream*)) j2k_compress_tile;
		codec->m_codec_data.m_compression.destroy =
				(void (*)(void*)) j2k_destroy;
		codec->m_codec_data.m_compression.init_compress =
				(bool (*)(void*,  grk_cparameters  * , grk_image * )) j2k_init_compress;
		codec->m_codec = new CodeStream(false);
		if (!codec->m_codec) {
			grk_free(codec);
			return nullptr;
		}
		break;
	case GRK_CODEC_JP2:
		/* get a JP2 decoder handle */
		codec->m_codec_data.m_compression.compress =
				(bool (*)(void*, grk_plugin_tile*, BufferedStream*)) jp2_compress;
		codec->m_codec_data.m_compression.end_compress = (bool (*)(void*,
				BufferedStream*)) jp2_end_compress;
		codec->m_codec_data.m_compression.start_compress = (bool (*)(void*,
				BufferedStream*)) jp2_start_compress;
		codec->m_codec_data.m_compression.write_tile =
				(bool (*)(void*, TileProcessor*, uint16_t, uint8_t*, uint64_t, BufferedStream*)) jp2_compress_tile;
		codec->m_codec_data.m_compression.destroy =
				(void (*)(void*)) jp2_destroy;
		codec->m_codec_data.m_compression.init_compress =
				(bool (*)(void*,  grk_cparameters  * , grk_image * )) jp2_init_compress;

		codec->m_codec = new FileFormat(false);
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
		if (!codec->is_decompressor) {
			return codec->m_codec_data.m_compression.init_compress(
					codec->m_codec, parameters, p_image);
		}
	}
	return false;
}
bool GRK_CALLCONV grk_start_compress( grk_codec p_codec) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		auto stream = (BufferedStream*) codec->m_stream;
		if (!codec->is_decompressor) {
			return codec->m_codec_data.m_compression.start_compress(
					codec->m_codec, stream	);
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
		auto stream = (BufferedStream*) codec->m_stream;
		if (!codec->is_decompressor) {
			return codec->m_codec_data.m_compression.compress(codec->m_codec,
					tile, stream);
		}
	}
	return false;
}
bool GRK_CALLCONV grk_end_compress( grk_codec p_codec) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		auto stream = (BufferedStream*) codec->m_stream;
		if (!codec->is_decompressor) {
			return codec->m_codec_data.m_compression.end_compress(
					codec->m_codec, stream);
		}
	}
	return false;
}
bool GRK_CALLCONV grk_end_decompress( grk_codec p_codec) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		auto stream = (BufferedStream*) codec->m_stream;
		if (!codec->is_decompressor) {
			return false;
		}
		return codec->m_codec_data.m_decompression.end_decompress(
				codec->m_codec, stream);
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
		auto stream = (BufferedStream*) codec->m_stream;
		if (codec->is_decompressor)
			return false;
		return codec->m_codec_data.m_compression.write_tile(codec->m_codec,
				nullptr, tile_index, p_data, data_size, stream	);
	}
	return false;
}

/* ---------------------------------------------------------------------- */

void GRK_CALLCONV grk_destroy_codec( grk_codec p_codec) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		if (codec->is_decompressor)
			codec->m_codec_data.m_decompression.destroy(codec->m_codec);
		else
			codec->m_codec_data.m_compression.destroy(codec->m_codec);
		codec->m_codec = nullptr;
		grk_free(codec);
	}
}

/* ---------------------------------------------------------------------- */

void GRK_CALLCONV grk_dump_codec( grk_codec p_codec, uint32_t info_flag,
		FILE *output_stream) {
	assert(p_codec);
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		codec->grk_dump_codec(codec->m_codec, info_flag, output_stream);
	}
}
 grk_codestream_info_v2  *  GRK_CALLCONV grk_get_cstr_info( grk_codec p_codec) {
	if (p_codec) {
		auto codec = (grk_codec_private*) p_codec;
		return codec->get_codec_info(codec->m_codec);
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
		return codec->grk_get_codec_index(codec->m_codec);
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

static void grok_free_file(void *p_user_data) {
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
					stdin_stdout ? nullptr : grok_free_file));
	if (is_read_stream)
		grk_stream_set_user_data_length(stream,
				grk_get_data_length_from_file(p_file));
	grk_stream_set_read_function(stream,
			(grk_stream_read_fn) grk_read_from_file);
	grk_stream_set_write_function(stream,
			(grk_stream_write_fn) grk_write_from_file);
	grk_stream_set_seek_function(stream,
			(grk_stream_seek_fn) grok_seek_from_file);
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
 grk_stream  *  GRK_CALLCONV grk_stream_create_mapped_file_read_stream(
		const char *fname) {
	return create_mapped_file_read_stream(fname);
}
/* ---------------------------------------------------------------------- */
void GRK_CALLCONV grk_image_all_components_data_free(grk_image *image) {
	uint32_t i;
	if (!image || !image->comps)
		return;
	for (i = 0; i < image->numcomps; ++i)
		grk_image_single_component_data_free(image->comps + i);
}
bool GRK_CALLCONV grk_image_single_component_data_alloc(
		 grk_image_comp  *comp) {
	if (!comp)
		return false;
	comp->stride = grk_make_aligned_width(comp->w);
	auto data = (int32_t*) grk_aligned_malloc(
			(uint64_t) comp->stride * comp->h * sizeof(uint32_t));
	if (!data)
		return false;
	grk_image_single_component_data_free(comp);
	comp->data = data;
	comp->owns_data = true;
	return true;
}
void GRK_CALLCONV grk_image_single_component_data_free( grk_image_comp  *comp) {
	if (!comp || !comp->data || !comp->owns_data)
		return;
	grk_aligned_free(comp->data);
	comp->data = nullptr;
	comp->owns_data = false;
}

uint8_t* GRK_CALLCONV grk_buffer_new(size_t len) {
	return new uint8_t[len];
}
void GRK_CALLCONV grk_buffer_delete(uint8_t *buf) {
	delete[] buf;
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
