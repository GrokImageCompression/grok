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

#ifdef _WIN32
#include <windows.h>
#else /* _WIN32 */
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#endif
#include <fcntl.h>

#include "grk_includes.h"
using namespace grk;

struct GrkCodec
{
   explicit GrkCodec(grk_stream* stream);
   ~GrkCodec();

   static GrkCodec* getImpl(grk_codec* codec)
   {
	  return ((GrkObjectWrapperImpl<GrkCodec>*)codec->wrapper)->getWrappee();
   }
   grk_codec* getWrapper(void)
   {
	  return &obj;
   }

   grk_object obj;
   ICodeStreamCompress* compressor_;
   ICodeStreamDecompress* decompressor_;

 private:
   grk_stream* stream_;
};

GrkCodec::GrkCodec(grk_stream* stream)
	: compressor_(nullptr), decompressor_(nullptr), stream_(stream)
{
   obj.wrapper = new GrkObjectWrapperImpl<GrkCodec>(this);
}

GrkCodec::~GrkCodec()
{
   delete compressor_;
   delete decompressor_;
   grk_object_unref(stream_);
}

/**
 * Start compressing image
 *
 * @param codec         compression codec
 *
 */
static bool grk_compress_start(grk_codec* codec);

/** Create stream from a file identified with its filename with a specific buffer size
 *
 * @param fname           the name of the file to stream
 * @param buffer_size     size of the chunk used to stream
 * @param is_read_stream  whether the stream is a read stream (true) or not (false)
 */
static grk_stream* grk_stream_create_file_stream(const char* fname, size_t buffer_size,
												 bool is_read_stream);

static grk_stream* grk_stream_create_stream(grk_stream_params* stream_params);

static grk_stream* grk_stream_new(size_t buffer_size, bool is_input)
{
   auto streamImpl = new BufferedStream(nullptr, buffer_size, is_input);

   return streamImpl->getWrapper();
}

grk_codec* grk_decompress_create(grk_stream* stream)
{
   GrkCodec* codec = nullptr;
   auto bstream = BufferedStream::getImpl(stream);
   auto format = bstream->getFormat();
   if(format == GRK_CODEC_UNK)
   {
	  Logger::logger_.error("Invalid codec format.");
	  return nullptr;
   }
   codec = new GrkCodec(stream);
   if(format == GRK_CODEC_J2K)
	  codec->decompressor_ = new CodeStreamDecompress(BufferedStream::getImpl(stream));
   else
	  codec->decompressor_ = new FileFormatDecompress(BufferedStream::getImpl(stream));

   return &codec->obj;
}

static bool is_plugin_initialized = false;
bool GRK_CALLCONV grk_initialize(const char* pluginPath, uint32_t numthreads, bool verbose)
{
   ExecSingleton::instance(numthreads);
   if(!is_plugin_initialized)
   {
	  grk_plugin_load_info info;
	  info.pluginPath = pluginPath;
	  info.verbose = verbose;
	  is_plugin_initialized = grk_plugin_load(info);
	  if(!is_plugin_initialized)
	    return false;
	  else
	    Logger::logger_.info("Plugin loaded");
   }

   return true;
}

GRK_API void GRK_CALLCONV grk_deinitialize()
{
   grk_plugin_cleanup();
   ExecSingleton::destroy();
}

GRK_API grk_object* GRK_CALLCONV grk_object_ref(grk_object* obj)
{
   if(!obj)
	  return nullptr;
   auto wrapper = (GrkObjectWrapper*)obj->wrapper;
   wrapper->ref();

   return obj;
}
GRK_API void GRK_CALLCONV grk_object_unref(grk_object* obj)
{
   if(!obj)
	  return;
   GrkObjectWrapper* wrapper = (GrkObjectWrapper*)obj->wrapper;
   if(wrapper->unref() == 0)
	  delete wrapper;
}

GRK_API void GRK_CALLCONV grk_set_msg_handlers(grk_msg_callback info_callback, void* info_user_data,
											   grk_msg_callback warn_callback, void* warn_user_data,
											   grk_msg_callback error_callback,
											   void* error_user_data)
{
   Logger::logger_.info_handler = info_callback;
   Logger::logger_.info_data_ = info_user_data;
   Logger::logger_.warning_handler = warn_callback;
   Logger::logger_.warning_data_ = warn_user_data;
   Logger::logger_.error_handler = error_callback;
   Logger::logger_.error_data_ = error_user_data;
}

static size_t grk_read_from_file(uint8_t* buffer, size_t numBytes, void* p_file)
{
   return fread(buffer, 1, numBytes, (FILE*)p_file);
}

static uint64_t grk_get_data_length_from_file(void* filePtr)
{
   auto file = (FILE*)filePtr;
   GRK_FSEEK(file, 0, SEEK_END);
   int64_t file_length = (int64_t)GRK_FTELL(file);
   GRK_FSEEK(file, 0, SEEK_SET);
   return (uint64_t)file_length;
}
static size_t grk_write_to_file(const uint8_t* buffer, size_t numBytes, void* p_file)
{
   return fwrite(buffer, 1, numBytes, (FILE*)p_file);
}
static bool grk_seek_in_file(uint64_t numBytes, void* p_user_data)
{
   if(numBytes > INT64_MAX)
   {
	  return false;
   }

   return GRK_FSEEK((FILE*)p_user_data, (int64_t)numBytes, SEEK_SET) ? false : true;
}

#ifdef _WIN32
#ifndef GRK_STATIC
BOOL APIENTRY DllMain([[maybe_unused]] HINSTANCE hModule, DWORD ul_reason_for_call,
					  [[maybe_unused]] LPVOID lpReserved)
{
   switch(ul_reason_for_call)
   {
	  case DLL_PROCESS_ATTACH:
		 break;
	  case DLL_PROCESS_DETACH:
		 break;
	  case DLL_THREAD_ATTACH:
	  case DLL_THREAD_DETACH:
		 break;
   }
   return TRUE;
}
#endif /* GRK_STATIC */
#endif /* _WIN32 */

const char* GRK_CALLCONV grk_version(void)
{
   return GRK_PACKAGE_VERSION;
}

grk_image* GRK_CALLCONV grk_image_new(uint16_t numcmpts, grk_image_comp* cmptparms,
									  GRK_COLOR_SPACE clrspc, bool alloc_data)
{
   return GrkImage::create(nullptr, numcmpts, cmptparms, clrspc, alloc_data);
}

grk_image_meta* GRK_CALLCONV grk_image_meta_new(void)
{
   return (grk_image_meta*)(new GrkImageMeta());
}

/* DECOMPRESSION FUNCTIONS*/

static const char* JP2_RFC3745_MAGIC = "\x00\x00\x00\x0c\x6a\x50\x20\x20\x0d\x0a\x87\x0a";
static const char* J2K_CODESTREAM_MAGIC = "\xff\x4f\xff\x51";
bool grk_decompress_buffer_detect_format(uint8_t* buffer, size_t len, GRK_CODEC_FORMAT* fmt)
{
   GRK_CODEC_FORMAT magic_format = GRK_CODEC_UNK;
   if(len < 12)
	  return false;

   if(memcmp(buffer, JP2_RFC3745_MAGIC, 12) == 0)
   {
	  magic_format = GRK_CODEC_JP2;
   }
   else if(memcmp(buffer, J2K_CODESTREAM_MAGIC, 4) == 0)
   {
	  magic_format = GRK_CODEC_J2K;
   }
   else
   {
	  Logger::logger_.error("No JPEG 2000 code stream detected.");
	  *fmt = GRK_CODEC_UNK;

	  return false;
   }
   *fmt = magic_format;

   return true;
}
bool GRK_CALLCONV grk_decompress_detect_format(const char* fileName, GRK_CODEC_FORMAT* fmt)
{
   uint8_t buf[12];
   size_t bytesRead;

   auto reader = fopen(fileName, "rb");
   if(!reader)
	  return false;

   bytesRead = fread(buf, 1, 12, reader);
   if(fclose(reader))
	  return false;
   if(bytesRead != 12)
	  return false;

   return grk_decompress_buffer_detect_format(buf, 12, fmt);
}

void GRK_CALLCONV grk_set_default_stream_params(grk_stream_params* params)
{
   memset(params, 0, sizeof(grk_stream_params));
}

static grk_codec* grk_decompress_create_from_buffer(uint8_t* buf, size_t len)
{
   auto stream = create_mem_stream(buf, len, false, true);
   if(!stream)
   {
	  Logger::logger_.error("Unable to create memory stream.");
	  return nullptr;
   }
   auto codec = grk_decompress_create(stream);
   if(!codec)
   {
	  Logger::logger_.error("Unable to create codec");
	  grk_object_unref(stream);
	  return nullptr;
   }

   return codec;
}

static grk_codec* grk_decompress_create_from_callbacks(grk_stream_params* stream_params)
{
   auto stream = grk_stream_create_stream(stream_params);
   if(!stream)
   {
	  Logger::logger_.error("Unable to create callback stream.");
	  return nullptr;
   }
   auto codec = grk_decompress_create(stream);
   if(!codec)
   {
	  Logger::logger_.error("Unable to create codec");
	  grk_object_unref(stream);
	  return nullptr;
   }

   return codec;
}

static grk_codec* grk_decompress_create_from_file(const char* file_name)
{
   auto stream = grk_stream_create_file_stream(file_name, 1000000, true);
   if(!stream)
   {
	  Logger::logger_.error("Unable to create stream for file %s.", file_name);
	  return nullptr;
   }
   auto codec = grk_decompress_create(stream);
   if(!codec)
   {
	  Logger::logger_.error("Unable to create codec for file %s", file_name);
	  grk_object_unref(stream);
	  return nullptr;
   }

   return codec;
}
void GRK_CALLCONV grk_decompress_set_default_params(grk_decompress_parameters* parameters)
{
   if(!parameters)
	  return;
   memset(parameters, 0, sizeof(grk_decompress_parameters));
   auto core_params = &parameters->core;
   memset(core_params, 0, sizeof(grk_decompress_core_params));
   core_params->tile_cache_strategy = GRK_TILE_CACHE_NONE;
   core_params->random_access_flags_ =
	   GRK_RANDOM_ACCESS_TLM | GRK_RANDOM_ACCESS_PLM | GRK_RANDOM_ACCESS_PLT;
}
grk_codec* GRK_CALLCONV grk_decompress_init(grk_stream_params* stream_params,
											grk_decompress_core_params* core_params)
{
   if(!stream_params || !core_params)
	  return nullptr;
   grk_codec* codecWrapper = nullptr;
   if(stream_params->file)
	  codecWrapper = grk_decompress_create_from_file(stream_params->file);
   else if(stream_params->buf)
	  codecWrapper = grk_decompress_create_from_buffer(stream_params->buf, stream_params->buf_len);
   else if(stream_params->read_fn)
   {
	  codecWrapper = grk_decompress_create_from_callbacks(stream_params);
   }
   if(!codecWrapper)
	  return nullptr;

   auto codec = GrkCodec::getImpl(codecWrapper);
   if(!codec->decompressor_)
   {
	  grk_object_unref(codecWrapper);

	  return nullptr;
   }

   codec->decompressor_->init(core_params);

   return codecWrapper;
}
bool GRK_CALLCONV grk_decompress_read_header(grk_codec* codecWrapper, grk_header_info* header_info)
{
   if(codecWrapper)
   {
	  auto codec = GrkCodec::getImpl(codecWrapper);
	  if(!codec->decompressor_)
		 return false;
	  bool rc = codec->decompressor_->readHeader(header_info);
	  rc &= codec->decompressor_->preProcess();

	  return rc;
   }
   return false;
}
bool GRK_CALLCONV grk_decompress_set_window(grk_codec* codecWrapper, float start_x, float start_y,
											float end_x, float end_y)
{
   if(codecWrapper)
   {
	  auto codec = GrkCodec::getImpl(codecWrapper);
	  return codec->decompressor_ ? codec->decompressor_->setDecompressRegion(
										grk_rect_single(start_x, start_y, end_x, end_y))
								  : false;
   }
   return false;
}
bool GRK_CALLCONV grk_decompress(grk_codec* codecWrapper, grk_plugin_tile* tile)
{
   if(codecWrapper)
   {
	  auto codec = GrkCodec::getImpl(codecWrapper);
	  bool rc = codec->decompressor_ ? codec->decompressor_->decompress(tile) : false;
	  rc = rc && (codec->decompressor_ ? codec->decompressor_->postProcess() : false);

	  return rc;
   }
   return false;
}
bool GRK_CALLCONV grk_decompress_tile(grk_codec* codecWrapper, uint16_t tile_index)
{
   if(codecWrapper)
   {
	  auto codec = GrkCodec::getImpl(codecWrapper);
	  bool rc = codec->decompressor_ ? codec->decompressor_->decompressTile(tile_index) : false;
	  rc = rc && (codec->decompressor_ ? codec->decompressor_->postProcess() : false);
	  return rc;
   }
   return false;
}
void GRK_CALLCONV grk_dump_codec(grk_codec* codecWrapper, uint32_t info_flag, FILE* output_stream)
{
   assert(codecWrapper);
   if(codecWrapper)
   {
	  auto codec = GrkCodec::getImpl(codecWrapper);
	  if(codec->decompressor_)
		 codec->decompressor_->dump(info_flag, output_stream);
   }
}

bool GRK_CALLCONV grk_set_MCT(grk_cparameters* parameters, float* pEncodingMatrix,
							  int32_t* p_dc_shift, uint32_t pNbComp)
{
   uint32_t l_matrix_size = pNbComp * pNbComp * (uint32_t)sizeof(float);
   uint32_t l_dc_shift_size = pNbComp * (uint32_t)sizeof(int32_t);
   uint32_t l_mct_total_size = l_matrix_size + l_dc_shift_size;

   /* add MCT capability */
   if(GRK_IS_PART2(parameters->rsiz))
   {
	  parameters->rsiz |= GRK_EXTENSION_MCT;
   }
   else
   {
	  parameters->rsiz = ((GRK_PROFILE_PART2) | (GRK_EXTENSION_MCT));
   }
   parameters->irreversible = true;

   /* use array based MCT */
   parameters->mct = 2;
   parameters->mct_data = grk_malloc(l_mct_total_size);
   if(!parameters->mct_data)
   {
	  return false;
   }
   memcpy(parameters->mct_data, pEncodingMatrix, l_matrix_size);
   memcpy(((uint8_t*)parameters->mct_data) + l_matrix_size, p_dc_shift, l_dc_shift_size);
   return true;
}
grk_image* GRK_CALLCONV grk_decompress_get_tile_image(grk_codec* codecWrapper, uint16_t tile_index)
{
   if(codecWrapper)
   {
	  auto codec = GrkCodec::getImpl(codecWrapper);
	  return codec->decompressor_ ? codec->decompressor_->getImage(tile_index) : nullptr;
   }
   return nullptr;
}

grk_image* GRK_CALLCONV grk_decompress_get_composited_image(grk_codec* codecWrapper)
{
   if(codecWrapper)
   {
	  auto codec = GrkCodec::getImpl(codecWrapper);
	  return codec->decompressor_ ? codec->decompressor_->getImage() : nullptr;
   }
   return nullptr;
}

/* COMPRESSION FUNCTIONS*/

grk_codec* GRK_CALLCONV grk_compress_create(GRK_CODEC_FORMAT p_format, grk_stream* stream)
{
   GrkCodec* codec = nullptr;
   switch(p_format)
   {
	  case GRK_CODEC_J2K:
		 codec = new GrkCodec(stream);
		 codec->compressor_ = new CodeStreamCompress(BufferedStream::getImpl(stream));
		 break;
	  case GRK_CODEC_JP2:
		 codec = new GrkCodec(stream);
		 codec->compressor_ = new FileFormatCompress(BufferedStream::getImpl(stream));
		 break;
	  default:
		 return nullptr;
   }
   return &codec->obj;
}
void GRK_CALLCONV grk_compress_set_default_params(grk_cparameters* parameters)
{
   if(!parameters)
	  return;

   memset(parameters, 0, sizeof(grk_cparameters));
   /* default coding parameters */
   parameters->rsiz = GRK_PROFILE_NONE;
   parameters->max_comp_size = 0;
   parameters->numresolution = GRK_DEFAULT_NUMRESOLUTION;
   parameters->cblockw_init = GRK_COMP_PARAM_DEFAULT_CBLOCKW;
   parameters->cblockh_init = GRK_COMP_PARAM_DEFAULT_CBLOCKH;
   parameters->numgbits = 2;
   parameters->prog_order = GRK_DEFAULT_PROG_ORDER;
   parameters->roi_compno = -1; /* no ROI */
   parameters->subsampling_dx = 1;
   parameters->subsampling_dy = 1;
   parameters->enable_tile_part_generation = false;
   parameters->decod_format = GRK_FMT_UNK;
   parameters->cod_format = GRK_FMT_UNK;
   parameters->layer_rate[0] = 0;
   parameters->numlayers = 0;
   parameters->allocation_by_rate_distortion = false;
   parameters->allocation_by_quality = false;
   parameters->write_plt = false;
   parameters->write_tlm = false;
   parameters->device_id = 0;
   parameters->repeats = 1;
}
grk_codec* GRK_CALLCONV grk_compress_init(grk_stream_params* stream_params,
										  grk_cparameters* parameters, grk_image* p_image)
{
   if(!parameters || !p_image)
	  return nullptr;
   if(parameters->cod_format != GRK_FMT_J2K && parameters->cod_format != GRK_FMT_JP2)
   {
	  Logger::logger_.error("Unknown stream format.");
	  return nullptr;
   }
   grk_stream* stream = nullptr;
   if(stream_params->buf)
   {
	  // let stream clean up compress buffer
	  stream = create_mem_stream(stream_params->buf, stream_params->buf_len, false, false);
   }
   else if(stream_params->file)
   {
	  stream = grk_stream_create_file_stream(stream_params->file, 1024 * 1024, false);
   }
   else if(stream_params->write_fn)
   {
	  stream = grk_stream_create_stream(stream_params);
   }
   if(!stream)
   {
	  Logger::logger_.error("failed to create stream");
	  return nullptr;
   }

   grk_codec* codecWrapper = nullptr;
   switch(parameters->cod_format)
   {
	  case GRK_FMT_J2K: /* JPEG 2000 code stream */
		 codecWrapper = grk_compress_create(GRK_CODEC_J2K, stream);
		 break;
	  case GRK_FMT_JP2: /* JPEG 2000 compressed image data */
		 codecWrapper = grk_compress_create(GRK_CODEC_JP2, stream);
		 break;
	  default:
		 break;
   }

   auto codec = GrkCodec::getImpl(codecWrapper);
   bool rc = codec->compressor_ ? codec->compressor_->init(parameters, (GrkImage*)p_image) : false;
   if(rc)
   {
	  rc = grk_compress_start(codecWrapper);
   }
   else
   {
	  Logger::logger_.error("Failed to initialize codec.");
	  grk_object_unref(codecWrapper);
	  codecWrapper = nullptr;
   }

   return rc ? codecWrapper : nullptr;
}
static bool grk_compress_start(grk_codec* codecWrapper)
{
   if(codecWrapper)
   {
	  auto codec = GrkCodec::getImpl(codecWrapper);
	  return codec->compressor_ ? codec->compressor_->start() : false;
   }
   return false;
}

uint64_t GRK_CALLCONV grk_compress(grk_codec* codecWrapper, grk_plugin_tile* tile)
{
   if(codecWrapper)
   {
	  auto codec = GrkCodec::getImpl(codecWrapper);
	  return codec->compressor_ ? codec->compressor_->compress(tile) : 0;
   }
   return 0;
}
static void grkFree_file(void* p_user_data)
{
   if(p_user_data)
	  fclose((FILE*)p_user_data);
}

static grk_stream* grk_stream_create_stream(grk_stream_params* stream_params)
{
   bool readStream = stream_params->read_fn;
   size_t doubleBufferLen =
	   stream_params->double_buffer_len ? stream_params->double_buffer_len : 1024 * 1024;
   if(stream_params->stream_len)
	  doubleBufferLen = std::min(doubleBufferLen, stream_params->stream_len);
   auto stream = grk_stream_new(doubleBufferLen, readStream);
   if(!stream)
	  return nullptr;
   // validate
   if(readStream)
   {
	  auto bstream = BufferedStream::getImpl(stream);
	  uint8_t buf[12];
	  size_t bytesRead = stream_params->read_fn(buf, 12, stream_params->user_data);
	  if(bytesRead != 12)
		 return nullptr;
	  stream_params->seek_fn(0, stream_params->user_data);
	  GRK_CODEC_FORMAT fmt;
	  if(!grk_decompress_buffer_detect_format(buf, 12, &fmt))
	  {
		 Logger::logger_.error("Unable to detect codec format.");
		 return nullptr;
	  }
	  bstream->setFormat(fmt);
   }

   grk_stream_set_user_data(stream, stream_params->user_data, stream_params->free_user_data_fn);
   if(readStream)
	  grk_stream_set_user_data_length(stream, stream_params->stream_len);
   grk_stream_set_read_function(stream, stream_params->read_fn);
   grk_stream_set_write_function(stream, stream_params->write_fn);
   grk_stream_set_seek_function(stream, stream_params->seek_fn);

   return stream;
}

static grk_stream* grk_stream_create_file_stream(const char* fname, size_t buffer_size,
												 bool is_read_stream)
{
   bool stdin_stdout = !fname || !fname[0];
   FILE* file = nullptr;
   if(stdin_stdout)
   {
	  file = is_read_stream ? stdin : stdout;
   }
   else
   {
	  const char* mode = (is_read_stream) ? "rb" : "wb";
	  file = fopen(fname, mode);
	  if(!file)
		 return nullptr;
   }
   auto stream = grk_stream_new(buffer_size, is_read_stream);
   if(!stream)
   {
	  if(!stdin_stdout)
		 fclose(file);
	  return nullptr;
   }
   // validate
   if(is_read_stream)
   {
	  uint8_t buf[12];
	  size_t bytesRead = fread(buf, 1, 12, file);
	  if(bytesRead != 12)
		 return nullptr;
	  rewind(file);
	  auto bstream = BufferedStream::getImpl(stream);
	  GRK_CODEC_FORMAT fmt;
	  if(!grk_decompress_buffer_detect_format(buf, 12, &fmt))
	  {
		 Logger::logger_.error("Unable to detect codec format.");
		 return nullptr;
	  }
	  bstream->setFormat(fmt);
   }

   grk_stream_set_user_data(stream, file, stdin_stdout ? nullptr : grkFree_file);
   if(is_read_stream)
	  grk_stream_set_user_data_length(stream, grk_get_data_length_from_file(file));
   grk_stream_set_read_function(stream, grk_read_from_file);
   grk_stream_set_write_function(stream, grk_write_to_file);
   grk_stream_set_seek_function(stream, grk_seek_in_file);
   return stream;
}

/**********************************************************************
 Plugin interface implementation
 ***********************************************************************/

static const char* plugin_get_debug_state_method_name = "plugin_get_debug_state";
static const char* plugin_init_method_name = "plugin_init";
static const char* plugin_encode_method_name = "plugin_encode";
static const char* plugin_batch_encode_method_name = "plugin_batch_encode";
static const char* plugin_stop_batch_encode_method_name = "plugin_stop_batch_encode";
static const char* plugin_wait_for_batch_complete_method_name = "plugin_wait_for_batch_complete";
static const char* plugin_decode_method_name = "plugin_decompress";
static const char* plugin_init_batch_decode_method_name = "plugin_init_batch_decompress";
static const char* plugin_batch_decode_method_name = "plugin_batch_decompress";
static const char* plugin_stop_batch_decode_method_name = "plugin_stop_batch_decompress";

static const char* pathSeparator()
{
#ifdef _WIN32
   return "\\";
#else
   return "/";
#endif
}

bool pluginLoaded = false;
bool GRK_CALLCONV grk_plugin_load(grk_plugin_load_info info)
{
   if(!info.pluginPath)
	  return false;

   // form plugin name
   std::string pluginName = "";
#if !defined(_WIN32)
   pluginName += "lib";
#endif
   pluginName += std::string(GROK_PLUGIN_NAME) + "." + minpf_get_dynamic_library_extension();

   // form absolute plugin path
   auto pluginPath = std::string(info.pluginPath) + pathSeparator() + pluginName;
   int32_t rc = minpf_load_from_path(pluginPath.c_str(), info.verbose, nullptr);

   // if fails, try local path
   if(rc)
   {
	  std::string localPlugin = std::string(".") + pathSeparator() + pluginName;
	  rc = minpf_load_from_path(localPlugin.c_str(), info.verbose, nullptr);
   }
   pluginLoaded = !rc;
   if(!pluginLoaded)
	  minpf_cleanup_plugin_manager();
   return pluginLoaded;
}
uint32_t GRK_CALLCONV grk_plugin_get_debug_state()
{
   uint32_t rc = GRK_PLUGIN_STATE_NO_DEBUG;
   if(!pluginLoaded)
	  return rc;
   auto mgr = minpf_get_plugin_manager();
   if(mgr && mgr->num_libraries > 0)
   {
	  auto func = (PLUGIN_GET_DEBUG_STATE)minpf_get_symbol(mgr->dynamic_libraries[0],
														   plugin_get_debug_state_method_name);
	  if(func)
		 rc = func();
   }
   return rc;
}
void GRK_CALLCONV grk_plugin_cleanup(void)
{
   minpf_cleanup_plugin_manager();
   pluginLoaded = false;
}
GRK_API bool GRK_CALLCONV grk_plugin_init(grk_plugin_init_info initInfo)
{
   if(!pluginLoaded)
	  return false;
   auto mgr = minpf_get_plugin_manager();
   if(mgr && mgr->num_libraries > 0)
   {
	  auto func = (PLUGIN_INIT)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_init_method_name);
	  if(func)
		 return func(initInfo);
   }
   return false;
}

/*******************
 Encode Implementation
 ********************/

GRK_PLUGIN_COMPRESS_USER_CALLBACK userEncodeCallback = 0;

/* wrapper for user's compress callback */
uint64_t grk_plugin_internal_encode_callback(grk_plugin_compress_user_callback_info* info)
{
   uint64_t rc = 0;
   if(userEncodeCallback)
	  rc = userEncodeCallback(info);

   return rc;
}
int32_t GRK_CALLCONV grk_plugin_compress(grk_cparameters* compress_parameters,
										 GRK_PLUGIN_COMPRESS_USER_CALLBACK callback)
{
   if(!pluginLoaded)
	  return -1;
   userEncodeCallback = callback;
   auto mgr = minpf_get_plugin_manager();
   if(mgr && mgr->num_libraries > 0)
   {
	  auto func =
		  (PLUGIN_ENCODE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_encode_method_name);
	  if(func)
		 return func((grk_cparameters*)compress_parameters, grk_plugin_internal_encode_callback);
   }
   return -1;
}
int32_t GRK_CALLCONV grk_plugin_batch_compress(grk_plugin_compress_batch_info info)
{
   if(!pluginLoaded)
	  return -1;
   userEncodeCallback = info.callback;
   auto mgr = minpf_get_plugin_manager();
   info.callback = grk_plugin_internal_encode_callback;
   if(mgr && mgr->num_libraries > 0)
   {
	  auto func = (PLUGIN_BATCH_ENCODE)minpf_get_symbol(mgr->dynamic_libraries[0],
														plugin_batch_encode_method_name);
	  if(func)
		 return func(info);
   }
   return -1;
}

PLUGIN_WAIT_FOR_BATCH_COMPLETE funcPluginWaitForBatchComplete = nullptr;
GRK_API void GRK_CALLCONV grk_plugin_wait_for_batch_complete(void)
{
   if(!pluginLoaded)
	  return;
   auto mgr = minpf_get_plugin_manager();
   if(mgr && mgr->num_libraries > 0)
   {
	  if(!funcPluginWaitForBatchComplete)
		 funcPluginWaitForBatchComplete = (PLUGIN_WAIT_FOR_BATCH_COMPLETE)minpf_get_symbol(
			 mgr->dynamic_libraries[0], plugin_wait_for_batch_complete_method_name);
	  if(funcPluginWaitForBatchComplete)
		 funcPluginWaitForBatchComplete();
   }
}
void GRK_CALLCONV grk_plugin_stop_batch_compress(void)
{
   if(!pluginLoaded)
	  return;
   auto mgr = minpf_get_plugin_manager();
   if(mgr && mgr->num_libraries > 0)
   {
	  auto func = (PLUGIN_STOP_BATCH_ENCODE)minpf_get_symbol(mgr->dynamic_libraries[0],
															 plugin_stop_batch_encode_method_name);
	  if(func)
		 func();
   }
}

/*******************
 Decompress Implementation
 ********************/

grk_plugin_decompress_callback decodeCallback = 0;

/* wrapper for user's decompress callback */
int32_t grk_plugin_internal_decode_callback(PluginDecodeCallbackInfo* info)
{
   int32_t rc = -1;
   /* set code block data etc on code object */
   grk_plugin_decompress_callback_info grokInfo;
   memset(&grokInfo, 0, sizeof(grk_plugin_decompress_callback_info));
   grokInfo.init_decompressors_func = info->init_decompressors_func;
   grokInfo.input_file_name = info->inputFile.empty() ? nullptr : info->inputFile.c_str();
   grokInfo.output_file_name = info->outputFile.empty() ? nullptr : info->outputFile.c_str();
   grokInfo.decod_format = info->decod_format;
   grokInfo.cod_format = info->cod_format;
   grokInfo.decompressor_parameters = info->decompressor_parameters;
   grokInfo.codec = info->codec;
   grokInfo.image = info->image;
   grokInfo.plugin_owns_image = info->plugin_owns_image;
   grokInfo.tile = info->tile;
   grokInfo.decompress_flags = info->decompress_flags;
   grokInfo.user_data = info->decompressor_parameters->user_data;
   if(decodeCallback)
	  rc = decodeCallback(&grokInfo);
   // synch
   info->image = grokInfo.image;
   info->codec = grokInfo.codec;
   info->header_info = grokInfo.header_info;
   return rc;
}

int32_t GRK_CALLCONV grk_plugin_decompress(grk_decompress_parameters* decompress_parameters,
										   grk_plugin_decompress_callback callback)
{
   if(!pluginLoaded)
	  return -1;
   decodeCallback = callback;
   auto mgr = minpf_get_plugin_manager();
   if(mgr && mgr->num_libraries > 0)
   {
	  auto func =
		  (PLUGIN_DECODE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_decode_method_name);
	  if(func)
		 return func((grk_decompress_parameters*)decompress_parameters,
					 grk_plugin_internal_decode_callback);
   }
   return -1;
}
int32_t GRK_CALLCONV grk_plugin_init_batch_decompress(
	const char* input_dir, const char* output_dir, grk_decompress_parameters* decompress_parameters,
	grk_plugin_decompress_callback callback)
{
   if(!pluginLoaded)
	  return -1;
   decodeCallback = callback;
   auto mgr = minpf_get_plugin_manager();
   if(mgr && mgr->num_libraries > 0)
   {
	  auto func = (PLUGIN_INIT_BATCH_DECODE)minpf_get_symbol(mgr->dynamic_libraries[0],
															 plugin_init_batch_decode_method_name);
	  if(func)
		 return func(input_dir, output_dir, (grk_decompress_parameters*)decompress_parameters,
					 grk_plugin_internal_decode_callback);
   }
   return -1;
}
int32_t GRK_CALLCONV grk_plugin_batch_decompress(void)
{
   if(!pluginLoaded)
	  return -1;
   auto mgr = minpf_get_plugin_manager();
   if(mgr && mgr->num_libraries > 0)
   {
	  auto func = (PLUGIN_BATCH_DECODE)minpf_get_symbol(mgr->dynamic_libraries[0],
														plugin_batch_decode_method_name);
	  if(func)
		 return func();
   }
   return -1;
}
void GRK_CALLCONV grk_plugin_stop_batch_decompress(void)
{
   if(!pluginLoaded)
	  return;
   auto mgr = minpf_get_plugin_manager();
   if(mgr && mgr->num_libraries > 0)
   {
	  auto func = (PLUGIN_STOP_BATCH_DECODE)minpf_get_symbol(mgr->dynamic_libraries[0],
															 plugin_stop_batch_decode_method_name);
	  if(func)
		 func();
   }
}

void grk_stream_set_read_function(grk_stream* stream, grk_stream_read_fn func)
{
   auto streamImpl = BufferedStream::getImpl(stream);
   if((!streamImpl) || (!(streamImpl->getStatus() & GROK_STREAM_STATUS_INPUT)))
	  return;
   streamImpl->setReadFunction(func);
}

void grk_stream_set_seek_function(grk_stream* stream, grk_stream_seek_fn func)
{
   auto streamImpl = BufferedStream::getImpl(stream);
   if(streamImpl)
	  streamImpl->setSeekFunction(func);
}
void grk_stream_set_write_function(grk_stream* stream, grk_stream_write_fn func)
{
   auto streamImpl = BufferedStream::getImpl(stream);
   if((!streamImpl) || (!(streamImpl->getStatus() & GROK_STREAM_STATUS_OUTPUT)))
	  return;

   streamImpl->setWriteFunction(func);
}

void grk_stream_set_user_data(grk_stream* stream, void* p_data, grk_stream_free_user_data_fn func)
{
   auto streamImpl = BufferedStream::getImpl(stream);
   if(!streamImpl)
	  return;
   streamImpl->setUserData(p_data, func);
}
void grk_stream_set_user_data_length(grk_stream* stream, uint64_t data_length)
{
   auto streamImpl = BufferedStream::getImpl(stream);
   if(streamImpl)
	  streamImpl->setUserDataLength(data_length);
}
