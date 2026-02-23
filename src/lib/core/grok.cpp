/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

#include <cstdint>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#else /* _WIN32 */
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#endif
#include <fcntl.h>
#include <filesystem>
#include <mutex>

#if defined(GROK_HAVE_FSEEKO) && !defined(fseek)
#define fseek fseeko
#define ftell ftello
#endif
#if defined(_WIN32)
#define GRK_FSEEK(stream, offset, whence) _fseeki64(stream, /* __int64 */ offset, whence)
#define GRK_FTELL(stream) /* __int64 */ _ftelli64(stream)
#else
#define GRK_FSEEK(stream, offset, whence) fseek(stream, offset, whence)
#define GRK_FTELL(stream) ftell(stream)
#endif

#include "MinHeap.h"
#include "SequentialCache.h"
#include "SparseCache.h"
#include "CodeStreamLimits.h"
#include "geometry.h"
#include "MemManager.h"
#include "buffer.h"
#include "ChunkBuffer.h"
#include "minpf_plugin_manager.h"
#include "plugin_interface.h"
#include "TileWindow.h"
#include "GrkObjectWrapper.h"
#include "ChronoTimer.h"
#include "testing.h"
#include "MappedFile.h"
#include "GrkMatrix.h"
#include "Quantizer.h"
#include "SparseBuffer.h"
#include "ResSimple.h"
#include "SparseCanvas.h"
#include "intmath.h"
#include "ImageComponentFlow.h"
#include "MarkerCache.h"
#include "SlabPool.h"
#include "StreamIO.h"
#include "IStream.h"
#include "MemAdvisor.h"

#include "FetchCommon.h"
#include "TPFetchSeq.h"

#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "ICompressor.h"
#include "IDecompressor.h"

#include "MemStream.h"

#include "StreamGenerator.h"
#include "Profile.h"
#include "MarkerParser.h"
#include "Codec.h"

#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct TileProcessor;
struct TileProcessorCompress;
} // namespace grk
#include "PacketParser.h"
#include "PacketCache.h"
#include "CodingParams.h"
#include "CodeStream.h"
#include "PacketIter.h"

#include "PacketLengthCache.h"
#include "TLMMarker.h"
#include "ICoder.h"
#include "CoderPool.h"
#include "FileFormatJP2Family.h"
#include "FileFormatJP2Compress.h"
#include "FileFormatJP2Decompress.h"
#include "FileFormatMJ2.h"
#include "FileFormatMJ2Compress.h"
#include "FileFormatMJ2Decompress.h"

#include "BitIO.h"
#include "TagTree.h"

#include "Codeblock.h"
#include "CodeblockCompress.h"
#include "CodeblockDecompress.h"

#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"
#include "BlockExec.h"
#include "CodecScheduler.h"

#include "TileComponentWindow.h"

#include "TileProcessor.h"
#include "TileProcessorCompress.h"
#include "SOTMarker.h"
#include "CodeStreamCompress.h"
#include "TileCache.h"
#include "TileCompletion.h"
#include "CodeStreamDecompress.h"

using namespace grk;

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

static void infoCallback(const char* msg, [[maybe_unused]] void* client_data)
{
  auto t = std::string(msg) + "\n";
  fprintf(stdout, "[INFO] %s", t.c_str());
}
static void debugCallback(const char* msg, [[maybe_unused]] void* client_data)
{
  auto t = std::string(msg) + "\n";
  fprintf(stdout, "[DEBUG] %s", t.c_str());
}
static void traceCallback(const char* msg, [[maybe_unused]] void* client_data)
{
  auto t = std::string(msg) + "\n";
  fprintf(stdout, "[TRACE] %s", t.c_str());
}
static void warningCallback(const char* msg, [[maybe_unused]] void* client_data)
{
  auto t = std::string(msg) + "\n";
  fprintf(stdout, "[WARNING] %s", t.c_str());
}

static void errorCallback(const char* msg, [[maybe_unused]] void* client_data)
{
  auto t = std::string(msg) + "\n";
  fprintf(stderr, "%s", t.c_str());
}

static grk_object* grkDecompressCreate(grk::IStream* stream)
{
  Codec* codec = nullptr;
  auto bstream = stream;
  auto format = bstream->getFormat();
  if(format == GRK_CODEC_UNK)
  {
    grklog.error("Invalid codec format.");
    return nullptr;
  }
  codec = new Codec(stream);
  switch(format)
  {
    case GRK_CODEC_J2K:
      codec->decompressor_ = new CodeStreamDecompress(stream);
      break;
    case GRK_CODEC_JP2:
      codec->decompressor_ = new FileFormatJP2Decompress(stream);
      break;
    case GRK_CODEC_MJ2:
      codec->decompressor_ = new FileFormatMJ2Decompress(stream);
      break;
    default:
      delete codec;
      return nullptr;
      break;
  }

  return &codec->obj;
}

class GrkCleanup
{
public:
  GrkCleanup() = default;
  ~GrkCleanup()
  {
    grk_plugin_cleanup();
    ExecSingleton::destroy();
  }
};

static inline bool areStringsEqual(const char* lhs, const char* rhs)
{
  if(lhs == nullptr && rhs == nullptr)
  {
    return true;
  }
  if(lhs == nullptr || rhs == nullptr)
  {
    return false;
  }
  return std::strcmp(lhs, rhs) == 0;
}

struct InitState
{
  InitState(const char* pluginPath, uint32_t numThreads)
      : pluginPath_(pluginPath), numThreads_(numThreads), initialized_(false),
        pluginInitialized_(false)
  {}
  InitState(void) : InitState(nullptr, 0) {}
  bool operator==(const InitState& rhs) const
  {
    return areStringsEqual(pluginPath_, rhs.pluginPath_) && numThreads_ == rhs.numThreads_;
  }
  const char* pluginPath_;
  uint32_t numThreads_;
  bool initialized_;
  bool pluginInitialized_;
};

static InitState initState_;
static std::mutex initMutex;

void grk_initialize(const char* pluginPath, uint32_t numThreads, bool* plugin_initialized)
{
  if(plugin_initialized)
    *plugin_initialized = false;
  const char* singleThreadEnv = std::getenv("GRK_TEST_SINGLE");
  if(singleThreadEnv && std::atoi(singleThreadEnv) == 1)
    numThreads = 1; // Force single-threaded execution
  InitState newState(pluginPath, numThreads);
  {
    std::lock_guard<std::mutex> guard(initMutex);
    /*
    if library is initialized, then if either of the following conditions apply:
    1. plugin is initialized
    2. new state is identical to old state
    3. numThreads equals special value UINT32_MAX
    , then DO NOT re-initialize, and return right away
    */
    if(initState_.initialized_ &&
       (initState_.pluginInitialized_ || newState == initState_ || numThreads == UINT32_MAX))
    {
      if(plugin_initialized)
        *plugin_initialized = initState_.pluginInitialized_;
      return;
    }
    if(numThreads == UINT32_MAX)
      numThreads = 0;
    static GrkCleanup cleanup;

    // 1. set up executor
    ExecSingleton::create(numThreads);

    if(!Logger::logger_.info_handler)
    {
      grk_msg_handlers handlers = {};
      const char* debug_env = std::getenv("GRK_DEBUG");
      if(debug_env)
      {
        int level = std::atoi(debug_env);
        if(level >= 1)
          handlers.error_callback = errorCallback;
        if(level >= 2)
          handlers.warn_callback = warningCallback;
        if(level >= 3)
          handlers.info_callback = infoCallback;
        if(level >= 4)
          handlers.debug_callback = debugCallback;
        if(level >= 5)
          handlers.trace_callback = traceCallback;
      }
      grk_set_msg_handlers(handlers);
    }

    initState_ = newState;

    // 2. try to load plugin
    if(!initState_.pluginInitialized_)
    {
      grk_plugin_load_info info;
      info.pluginPath = pluginPath;
      initState_.pluginInitialized_ = grk_plugin_load(info);
      if(initState_.pluginInitialized_)
        grklog.info("Plugin loaded");
    }
    initState_.initialized_ = true;
  }
  if(plugin_initialized)
    *plugin_initialized = initState_.pluginInitialized_;
}

GRK_API grk_object* GRK_CALLCONV grk_object_ref(grk_object* obj)
{
  if(!obj)
    return nullptr;
  auto wrapper = (RefCounted*)obj->wrapper;
  wrapper->ref();

  return obj;
}
GRK_API void GRK_CALLCONV grk_object_unref(grk_object* obj)
{
  if(!obj)
    return;
  auto wrapper = (RefCounted*)obj->wrapper;
  wrapper->unref();
}

GRK_API void GRK_CALLCONV grk_set_msg_handlers(grk_msg_handlers msg_handlers)
{
  Logger::logger_.info_handler = msg_handlers.info_callback;
  Logger::logger_.info_data_ = msg_handlers.info_data;
  Logger::logger_.debug_handler = msg_handlers.debug_callback;
  Logger::logger_.debug_data_ = msg_handlers.debug_data;
  Logger::logger_.trace_handler = msg_handlers.trace_callback;
  Logger::logger_.trace_data_ = msg_handlers.trace_data;
  Logger::logger_.warning_handler = msg_handlers.warn_callback;
  Logger::logger_.warning_data_ = msg_handlers.warn_data;
  Logger::logger_.error_handler = msg_handlers.error_callback;
  Logger::logger_.error_data_ = msg_handlers.error_data;
}

const char* grk_version(void)
{
  return GRK_PACKAGE_VERSION;
}

grk_image* grk_image_new(uint16_t numcmpts, grk_image_comp* cmptparms, GRK_COLOR_SPACE clrspc,
                         bool alloc_data)
{
  return GrkImage::create(nullptr, numcmpts, cmptparms, clrspc, alloc_data);
}

grk_image_meta* grk_image_meta_new(void)
{
  return (grk_image_meta*)(new GrkImageMeta());
}

/* DECOMPRESSION FUNCTIONS*/
grk_object* grk_decompress_init(grk_stream_params* streamParams,
                                grk_decompress_parameters* decompressParams)
{
  if(!decompressParams)
  {
    grklog.error("grk_decompress_init: decompress parameters cannot be null");
    return nullptr;
  }

  if(!streamParams)
  {
    grklog.error("grk_decompress_init: stream parameters cannot be null"
                 " when creating decompression codec");
    return nullptr;
  }

  streamParams->is_read_stream = true;
  StreamGenerator sg(streamParams);
  auto stream = sg.create();
  if(!stream)
  {
    grklog.error("grk_decompress_init: stream is null");
    return nullptr;
  }
  auto codec = grkDecompressCreate(stream);
  if(!codec)
  {
    grklog.error("grk_decompress_init: Unable to create codec for file %s", streamParams->file);
    delete stream;
    return nullptr;
  }
  auto codecImpl = Codec::getImpl(codec);
  if(!codecImpl->decompressor_)
  {
    grk_object_unref(codec);

    return nullptr;
  }
  codecImpl->decompressor_->init(decompressParams);

  return codec;
}

grk_progression_state grk_decompress_get_progression_state(grk_object* codec, uint16_t tile_index)
{
  if(!codec)
  {
    std::cerr << "grk_decompress_get_progression_state: codec pointer cannot be null.\n";
    return {};
  }

  auto codecImpl = Codec::getImpl(codec);
  if(!codecImpl->decompressor_)
    return {};

  return codecImpl->decompressor_->getProgressionState(tile_index);
}

GRK_API bool GRK_CALLCONV grk_decompress_set_progression_state(grk_object* codec,
                                                               grk_progression_state state)
{
  if(!codec)
  {
    std::cerr << "grk_decompress_set_progression_state: codec pointer cannot be null.\n";
    return false;
  }

  auto codecImpl = Codec::getImpl(codec);
  if(!codecImpl->decompressor_)
    return false;

  return codecImpl->decompressor_->setProgressionState(state);
}

bool grk_decompress_update(grk_decompress_parameters* params, grk_object* codec)
{
  if(!params)
  {
    std::cerr << "grk_decompress_update: decompress parameters cannot be null.\n";
    return false;
  }
  if(!codec)
  {
    std::cerr << "grk_decompress_update: codec pointer cannot be null.\n";
    return false;
  }

  auto codecImpl = Codec::getImpl(codec);
  if(!codecImpl->decompressor_)
    return false;
  codecImpl->decompressor_->init(params);

  return true;
}

bool grk_decompress_read_header(grk_object* codecWrapper, grk_header_info* header_info)
{
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    if(!codec->decompressor_)
      return false;
    return codec->decompressor_->readHeader(header_info);
  }
  return false;
}
void grk_decompress_wait(grk_object* codecWrapper, grk_wait_swath* swath)
{
  if(!codecWrapper)
    return;

  auto codec = Codec::getImpl(codecWrapper);
  if(!codec->decompressor_)
    return;
  codec->decompressor_->wait(swath);
}
bool grk_decompress(grk_object* codecWrapper, grk_plugin_tile* tile)
{
  grk_initialize(nullptr, UINT32_MAX, nullptr);
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    return codec->decompressor_ ? codec->decompressor_->decompress(tile) : false;
  }
  return false;
}
bool grk_decompress_tile(grk_object* codecWrapper, uint16_t tile_index)
{
  if(!codecWrapper)
    return false;

  auto codec = Codec::getImpl(codecWrapper);
  if(ExecSingleton::num_threads() == 1)
  {
    return codec->decompressor_ ? codec->decompressor_->decompressTile(tile_index) : false;
  }
  else
  {
    auto f = codec->queueDecompressTile(tile_index);
    return f.get();
  }
}
void grk_dump_codec(grk_object* codecWrapper, uint32_t info_flag, FILE* output_stream)
{
  assert(codecWrapper);
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    if(codec->decompressor_)
      codec->decompressor_->dump(info_flag, output_stream);
  }
}

bool grk_set_MCT(grk_cparameters* parameters, const float* pEncodingMatrix,
                 const int32_t* p_dc_shift, uint32_t pNbComp)
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
grk_image* grk_decompress_get_tile_image(grk_object* codecWrapper, uint16_t tile_index, bool wait)
{
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    return codec->decompressor_ ? codec->decompressor_->getImage(tile_index, wait) : nullptr;
  }
  return nullptr;
}

grk_image* grk_decompress_get_image(grk_object* codecWrapper)
{
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    return codec->decompressor_ ? codec->decompressor_->getImage() : nullptr;
  }
  return nullptr;
}

/**
 * @brief Starts compressing image
 * @param codec         compression codec
 *
 */
static bool grkStartCompress(grk_object* codecWrapper)
{
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    return codec->compressor_ ? codec->compressor_->start() : false;
  }
  return false;
}

grk_object* grk_compress_create(GRK_CODEC_FORMAT p_format, grk::IStream* stream)
{
  Codec* codec = nullptr;
  switch(p_format)
  {
    case GRK_CODEC_J2K:
      codec = new Codec(stream);
      codec->compressor_ = new CodeStreamCompress(stream);
      break;
    case GRK_CODEC_JP2:
      codec = new Codec(stream);
      codec->compressor_ = new FileFormatJP2Compress(stream);
      break;
    default:
      return nullptr;
  }
  return &codec->obj;
}
void grk_compress_set_default_params(grk_cparameters* parameters)
{
  if(!parameters)
    return;
  *parameters = {};
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
grk_object* grk_compress_init(grk_stream_params* streamParams, grk_cparameters* parameters,
                              grk_image* image)
{
  if(!parameters || !image)
    return nullptr;
  if(parameters->cod_format != GRK_FMT_J2K && parameters->cod_format != GRK_FMT_JP2)
  {
    grklog.error("Unknown stream format.");
    return nullptr;
  }
  StreamGenerator sg(streamParams);
  grk::IStream* stream = sg.create();
  if(!stream)
  {
    grklog.error("failed to create stream");
    return nullptr;
  }

  grk_object* codecWrapper = nullptr;
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

  auto codec = Codec::getImpl(codecWrapper);
  bool rc = codec->compressor_ ? codec->compressor_->init(parameters, (GrkImage*)image) : false;
  if(rc)
  {
    rc = grkStartCompress(codecWrapper);
  }
  else
  {
    grklog.error("Failed to initialize codec.");
    grk_object_unref(codecWrapper);
    codecWrapper = nullptr;
  }

  return rc ? codecWrapper : nullptr;
}

uint64_t grk_compress(grk_object* codecWrapper, grk_plugin_tile* tile)
{
  grk_initialize(nullptr, UINT32_MAX, nullptr);
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    return codec->compressor_ ? codec->compressor_->compress(tile) : 0;
  }
  return 0;
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

bool pluginLoaded = false;
bool grk_plugin_load(grk_plugin_load_info info)
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
  auto pluginPath = std::string(info.pluginPath) +
                    static_cast<char>(std::filesystem::path::preferred_separator) + pluginName;
  int32_t rc = minpf_load_from_path(pluginPath.c_str(), nullptr);

  // if fails, try local path
  if(rc)
  {
    std::string localPlugin =
        std::string(".") + (char)std::filesystem::path::preferred_separator + pluginName;
    rc = minpf_load_from_path(localPlugin.c_str(), nullptr);
  }
  pluginLoaded = !rc;
  if(!pluginLoaded)
    minpf_cleanup_plugin_manager();
  return pluginLoaded;
}
uint32_t grk_plugin_get_debug_state()
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
void grk_plugin_cleanup(void)
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
int32_t grk_plugin_compress(grk_cparameters* compress_parameters,
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
int32_t grk_plugin_batch_compress(grk_plugin_compress_batch_info info)
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
void grk_plugin_stop_batch_compress(void)
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
  grk_plugin_decompress_callback_info grokInfo = {};
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

int32_t grk_plugin_decompress(grk_decompress_parameters* decompress_parameters,
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
int32_t grk_plugin_init_batch_decompress(const char* input_dir, const char* output_dir,
                                         grk_decompress_parameters* decompress_parameters,
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
int32_t grk_plugin_batch_decompress(void)
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
void grk_plugin_stop_batch_decompress(void)
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
