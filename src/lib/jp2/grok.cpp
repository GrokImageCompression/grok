/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
#include "ojph_block_encoder.h"
#include "ojph_block_decoder.h"
#include "grk_includes.h"
using namespace grk;

struct GrkCodec;

class GrkCodecObject : public GrkObjectWrapper {
public:
	explicit GrkCodecObject(GrkCodec *cdc) : codec(cdc)
	{}
	virtual ~GrkCodecObject(void) = default;
	virtual void release(void);
	GrkCodec *getCodec(){ return codec;}
private:
	GrkCodec *codec;
};

struct GrkCodec {
	GrkCodec();
	~GrkCodec();

	static GrkCodec* getImpl(grk_codec *codec){
		return ((GrkCodecObject*)codec->wrapper)->getCodec();
	}
	grk_codec* getWrapper(void){
		return &obj;
	}

	grk_object obj;
	ICodeStreamCompress *m_compressor;
	ICodeStreamDecompress *m_decompressor;
	grk_stream  *m_stream;
};

GrkCodec::GrkCodec() :
			m_compressor(nullptr),
			m_decompressor(nullptr),
			m_stream(nullptr){
	obj.wrapper = new GrkCodecObject(this);
}

void GrkCodecObject::release(void){
	delete codec;
}

GrkCodec::~GrkCodec(){
	delete m_compressor;
	delete m_decompressor;
}

ThreadPool* ThreadPool::singleton = nullptr;
std::mutex ThreadPool::singleton_mutex;

static bool is_plugin_initialized = false;
bool GRK_CALLCONV grk_initialize(const char *pluginPath, uint32_t numthreads) {
	ThreadPool::instance(numthreads);
	if (!is_plugin_initialized) {
		grk_plugin_load_info info;
		info.pluginPath = pluginPath;
		is_plugin_initialized = grk_plugin_load(info);
	}
	ojph::local::decode_vlc_init_tables();
    ojph::local::encode_vlc_init_tables();
    ojph::local::encode_uvlc_init_tables();

	return is_plugin_initialized;
}

GRK_API void GRK_CALLCONV grk_deinitialize() {
	grk_plugin_cleanup();
	ThreadPool::release();
}

GRK_API void GRK_CALLCONV grk_object_ref(grk_object *obj){
	if (!obj)
		return;
	GrkObjectWrapper* object = (GrkObjectWrapper*)obj->wrapper;

	object->ref();

}
GRK_API void GRK_CALLCONV grk_object_unref(grk_object *obj){
	if (!obj)
		return;
	GrkObjectWrapper* object = (GrkObjectWrapper*)obj->wrapper;
	assert(object->refcount());
	object->unref();
	if (object->refcount() == 0)
		delete object;

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

grk_image *  GRK_CALLCONV grk_image_new(uint16_t numcmpts,
		 grk_image_cmptparm  *cmptparms, GRK_COLOR_SPACE clrspc, bool allocData) {
	return GrkImage::create(numcmpts, cmptparms, clrspc, allocData);
}

grk_image_meta *  GRK_CALLCONV grk_image_meta_new(void){
	return (grk_image_meta*)(new GrkImageMeta());
}

void GRK_CALLCONV grk_image_all_components_data_free(grk_image *image) {
	uint32_t i;
	if (!image || !image->comps)
		return;
	for (i = 0; i < image->numcomps; ++i)
		grk_image_single_component_data_free(image->comps + i);
}

void GRK_CALLCONV grk_image_single_component_data_free( grk_image_comp  *comp) {
	if (!comp || !comp->data)
		return;
	grkAlignedFree(comp->data);
	comp->data = nullptr;
}

/* ---------------------------------------------------------------------- */
/* DECOMPRESSION FUNCTIONS*/
 grk_codec*   GRK_CALLCONV grk_decompress_create(GRK_CODEC_FORMAT p_format,
		 grk_stream  *stream) {
	auto codec = new GrkCodec();
	codec->m_stream = stream;

	switch (p_format) {
	case GRK_CODEC_J2K:
	{
		auto cstream = new CodeStreamDecompress(BufferedStream::getImpl(stream));
		codec->m_decompressor = cstream;
	}
		break;
	case GRK_CODEC_JP2:
	{
		auto ff = new FileFormatDecompress(BufferedStream::getImpl(stream));
		codec->m_decompressor = ff;
	}
		break;
	case GRK_CODEC_UNKNOWN:
	default:
		grk_free(codec);
		return nullptr;
	}
	return &codec->obj;
}
void GRK_CALLCONV grk_decompress_set_default_params(
		 grk_dparameters  *parameters) {
	if (parameters) {
		memset(parameters, 0, sizeof( grk_dparameters) );
		parameters->tileCacheStrategy = GRK_TILE_CACHE_NONE;
	}
}
bool GRK_CALLCONV grk_decompress_init( grk_codec *codecWrapper,
		 grk_dparameters  *parameters) {
	if (codecWrapper && parameters) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		if (codec->m_decompressor) {
			codec->m_decompressor->initDecompress(parameters) ;
			return true;
		}
		return false;
	}
	return false;
}
bool GRK_CALLCONV grk_decompress_read_header( grk_codec *codecWrapper,  grk_header_info  *header_info) {
	if (codecWrapper) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		return codec->m_decompressor ? codec->m_decompressor->readHeader( header_info) : false;
	}
	return false;
}
bool GRK_CALLCONV grk_decompress_set_window( grk_codec *codecWrapper,
		uint32_t start_x, uint32_t start_y,
		uint32_t end_x, uint32_t end_y) {
	if (codecWrapper) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		return codec->m_decompressor ?
				codec->m_decompressor->setDecompressWindow(grkRectU32(start_x, start_y, end_x,end_y))  : false;
	}
	return false;
}
bool GRK_CALLCONV grk_decompress( grk_codec *codecWrapper, grk_plugin_tile *tile) {
	if (codecWrapper) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		bool rc =  codec->m_decompressor ? codec->m_decompressor->decompress(tile) : false;
		//rc =  codec->m_decompressor ? codec->m_decompressor->decompress(tile) : false;
		return rc;
	}
	return false;
}
bool GRK_CALLCONV grk_decompress_tile( grk_codec *codecWrapper,uint16_t tile_index) {
	if (codecWrapper) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		bool rc;
		rc =  codec->m_decompressor ? codec->m_decompressor->decompressTile(tile_index) : false;
		return rc;
	}
	return false;
}
bool GRK_CALLCONV grk_decompress_end( grk_codec *codecWrapper) {
	if (codecWrapper) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		return codec->m_decompressor ? codec->m_decompressor->endDecompress() : false;
	}
	return false;
}


void GRK_CALLCONV grk_dump_codec( grk_codec *codecWrapper, uint32_t info_flag,
		FILE *output_stream) {
	assert(codecWrapper);
	if (codecWrapper) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		if (codec->m_decompressor)
			codec->m_decompressor->dump(info_flag, output_stream);
	}
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
grk_image* GRK_CALLCONV grk_decompress_get_tile_image( grk_codec *codecWrapper, uint16_t tileIndex) {
	if (codecWrapper) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		return codec->m_decompressor ? codec->m_decompressor->getImage(tileIndex) : nullptr;
	}
	return nullptr;
}

grk_image* GRK_CALLCONV grk_decompress_get_composited_image( grk_codec *codecWrapper) {
	if (codecWrapper) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		return codec->m_decompressor ? codec->m_decompressor->getImage() : nullptr;
	}
	return nullptr;
}


/* ---------------------------------------------------------------------- */
/* COMPRESSION FUNCTIONS*/

 grk_codec*   GRK_CALLCONV grk_compress_create(GRK_CODEC_FORMAT p_format,
		 grk_stream  *stream) {
	auto codec = new GrkCodec();
	codec->m_stream = stream;

	switch (p_format) {
	case GRK_CODEC_J2K:
	{
		auto cstream = new CodeStreamCompress(BufferedStream::getImpl(stream));
		codec->m_compressor = cstream;
	}
		break;
	case GRK_CODEC_JP2:
		codec->m_compressor = new FileFormatCompress(BufferedStream::getImpl(stream));;
		break;
	case GRK_CODEC_UNKNOWN:
	default:
		grk_free(codec);
		return nullptr;
	}
	return &codec->obj;
}
void GRK_CALLCONV grk_compress_set_default_params(
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
bool GRK_CALLCONV grk_compress_init( grk_codec *codecWrapper,
		 grk_cparameters  *parameters, grk_image *p_image) {
	if (codecWrapper && parameters && p_image) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		return codec->m_compressor ? codec->m_compressor->initCompress(parameters, (GrkImage*)p_image) : false;
	}
	return false;
}
bool GRK_CALLCONV grk_compress_start( grk_codec *codecWrapper) {
	if (codecWrapper) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		return codec->m_compressor ? codec->m_compressor->startCompress() : false;
	}
	return false;
}
bool GRK_CALLCONV grk_compress( grk_codec *codecWrapper) {
	return grk_compress_with_plugin(codecWrapper, nullptr);
}
bool GRK_CALLCONV grk_compress_with_plugin( grk_codec *codecWrapper,
		grk_plugin_tile *tile) {
	if (codecWrapper) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		return codec->m_compressor ? codec->m_compressor->compress(tile) : false;
	}
	return false;
}
bool GRK_CALLCONV grk_compress_end( grk_codec *codecWrapper) {
	if (codecWrapper) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		return codec->m_compressor ? codec->m_compressor->endCompress() : false;
	}
	return false;
}
bool GRK_CALLCONV grk_compress_tile( grk_codec *codecWrapper, uint16_t tile_index,
		uint8_t *p_data, uint64_t data_size) {
	if (codecWrapper && p_data) {
		auto codec = GrkCodec::getImpl(codecWrapper);
		return codec->m_compressor ? codec->m_compressor->compressTile(tile_index, p_data, data_size) : false;
	}
	return false;
}

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
	auto stream = grk_stream_new(p_size, is_read_stream);
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
GRK_API size_t GRK_CALLCONV grk_stream_get_write_mem_stream_length( grk_stream  *stream) {
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
static const char *plugin_decode_method_name = "plugin_decompress";
static const char *plugin_init_batch_decode_method_name =
		"plugin_init_batch_decompress";
static const char *plugin_batch_decode_method_name = "plugin_batch_decompress";
static const char *plugin_stop_batch_decode_method_name =
		"plugin_stop_batch_decompress";

static const char* get_path_separator() {
#ifdef _WIN32
	return "\\";
#else
	return "/";
#endif
}

bool pluginLoaded = false;
bool GRK_CALLCONV grk_plugin_load(grk_plugin_load_info info) {
	if (!info.pluginPath)
		return false;

	// form plugin name
	std::string pluginName = "";
#if !defined(_WIN32)
	pluginName += "lib";
#endif
	pluginName += std::string(GROK_PLUGIN_NAME) + "."
			+ minpf_get_dynamic_library_extension();

	// form absolute plugin path
	auto pluginPath = std::string(info.pluginPath) + get_path_separator()
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

GRK_PLUGIN_COMPRESS_USER_CALLBACK userEncodeCallback = 0;

/* wrapper for user's compress callback */
void grk_plugin_internal_encode_callback(
		plugin_encode_user_callback_info *info) {
	/* set code block data etc on code object */
	grk_plugin_compress_user_callback_info grk_info;
	memset(&grk_info, 0, sizeof(grk_plugin_compress_user_callback_info));
	grk_info.input_file_name = info->input_file_name;
	grk_info.outputFileNameIsRelative = info->outputFileNameIsRelative;
	grk_info.output_file_name = info->output_file_name;
	grk_info.compressor_parameters = ( grk_cparameters  * ) info->compressor_parameters;
	grk_info.image = (grk_image * ) info->image;
	grk_info.tile = (grk_plugin_tile*) info->tile;
	if (userEncodeCallback)
		userEncodeCallback(&grk_info);
}
int32_t GRK_CALLCONV grk_plugin_compress( grk_cparameters  *compress_parameters,
		GRK_PLUGIN_COMPRESS_USER_CALLBACK callback) {
	if (!pluginLoaded)
		return -1;
	userEncodeCallback = callback;
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_ENCODE) minpf_get_symbol(mgr->dynamic_libraries[0],
				plugin_encode_method_name);
		if (func)
			return func(( grk_cparameters  * ) compress_parameters,
					grk_plugin_internal_encode_callback);
	}
	return -1;
}
int32_t GRK_CALLCONV grk_plugin_batch_compress(const char *input_dir,
		const char *output_dir,  grk_cparameters  *compress_parameters,
		GRK_PLUGIN_COMPRESS_USER_CALLBACK callback) {
	if (!pluginLoaded)
		return -1;
	userEncodeCallback = callback;
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_BATCH_ENCODE) minpf_get_symbol(mgr->dynamic_libraries[0],
				plugin_batch_encode_method_name);
		if (func) {
			return func(input_dir, output_dir,
					( grk_cparameters  * ) compress_parameters,
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
 Decompress Implementation
 ********************/

grk_plugin_decompress_callback decodeCallback = 0;

/* wrapper for user's decompress callback */
int32_t grk_plugin_internal_decode_callback(PluginDecodeCallbackInfo *info) {
	int32_t rc = -1;
	/* set code block data etc on code object */
	grk_plugin_decompress_callback_info grokInfo;
	memset(&grokInfo, 0, sizeof(grk_plugin_decompress_callback_info));
	grokInfo.init_decompressors_func = info->init_decompressors_func;
	grokInfo.input_file_name =
			info->inputFile.empty() ? nullptr : info->inputFile.c_str();
	grokInfo.output_file_name =
			info->outputFile.empty() ? nullptr : info->outputFile.c_str();
	grokInfo.decod_format = info->decod_format;
	grokInfo.cod_format = info->cod_format;
	grokInfo.decompressor_parameters = info->decompressor_parameters;
	grokInfo.stream = info->stream;
	grokInfo.codec = info->codec;
	grokInfo.image = info->image;
	grokInfo.plugin_owns_image = info->plugin_owns_image;
	grokInfo.tile = info->tile;
	grokInfo.decompress_flags = info->decompress_flags;
	if (decodeCallback)
		rc = decodeCallback(&grokInfo);
	//synch
	info->image = grokInfo.image;
	info->stream = grokInfo.stream;
	info->codec = grokInfo.codec;
	info->header_info = grokInfo.header_info;
	return rc;
}

int32_t GRK_CALLCONV grk_plugin_decompress(
		grk_decompress_parameters *decompress_parameters,
		grk_plugin_decompress_callback callback) {
	if (!pluginLoaded)
		return -1;
	decodeCallback = callback;
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_DECODE) minpf_get_symbol(mgr->dynamic_libraries[0],
				plugin_decode_method_name);
		if (func)
			return func((grk_decompress_parameters*) decompress_parameters,
					grk_plugin_internal_decode_callback);
	}
	return -1;
}
int32_t GRK_CALLCONV grk_plugin_init_batch_decompress(const char *input_dir,
		const char *output_dir, grk_decompress_parameters *decompress_parameters,
		grk_plugin_decompress_callback callback) {
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
					(grk_decompress_parameters*) decompress_parameters,
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

grk_stream* GRK_CALLCONV grk_stream_new(size_t buffer_size, bool is_input) {
	auto streamImpl = new BufferedStream(nullptr, buffer_size,	is_input);

	return streamImpl->getWrapper();
}

void GRK_CALLCONV grk_stream_set_read_function(grk_stream *stream,
		grk_stream_read_fn p_function) {
	auto streamImpl = BufferedStream::getImpl(stream);
	if ((!streamImpl) || (!(streamImpl->m_status & GROK_STREAM_STATUS_INPUT)))
		return;
	streamImpl->m_read_fn = p_function;
}

void GRK_CALLCONV grk_stream_set_seek_function(grk_stream *stream,
		grk_stream_seek_fn p_function) {
	auto streamImpl = BufferedStream::getImpl(stream);
	if (streamImpl)
		streamImpl->m_seek_fn = p_function;
}
void GRK_CALLCONV grk_stream_set_write_function(grk_stream *stream,
		grk_stream_write_fn p_function) {
	auto streamImpl = BufferedStream::getImpl(stream);
	if ((!streamImpl) || (!(streamImpl->m_status & GROK_STREAM_STATUS_OUTPUT)))
		return;

	streamImpl->m_write_fn = p_function;
}

void GRK_CALLCONV grk_stream_set_user_data(grk_stream *stream, void *p_data,
		grk_stream_free_user_data_fn p_function) {
	auto streamImpl = BufferedStream::getImpl(stream);
	if (!streamImpl)
		return;
	streamImpl->m_user_data = p_data;
	streamImpl->m_free_user_data_fn = p_function;
}
void GRK_CALLCONV grk_stream_set_user_data_length(grk_stream *stream,
		uint64_t data_length) {
	auto streamImpl = BufferedStream::getImpl(stream);
	if (streamImpl)
		streamImpl->m_user_data_length = data_length;
}
