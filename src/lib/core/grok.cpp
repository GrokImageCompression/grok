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
#include <stdexcept>
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
#include <atomic>

#include "grk_fseek.h"
#include "TFSingleton.h"

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
#include "plugin_gpup_bridge.h"
#include "plugin_accelerate.h"
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
#include "mercury_fastpath.h"
#include "TileFutureManager.h"
#include "MarkerCache.h"
#include "SlabPool.h"
#include "StreamIO.h"
#include "IStream.h"
#include "MemAdvisor.h"

#include "FetchCommon.h"
#include "TPFetchSeq.h"

#include "GrkImage.h"
#include "ICompressor.h"
#include "IDecompressor.h"
#include "XYZTransform.h"

#include "MemStream.h"

#include "StreamGenerator.h"
#include "Profile.h"
#include "MarkerParser.h"
#include "Codec.h"

#include "GrkImageSIMD.h"

#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
struct ITileProcessorCompress;
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

#include "ITileProcessor.h"
#include "ITileProcessorCompress.h"
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
    TFSingleton::destroy();
  }
};

void grk_deinitialize(void)
{
  grk_plugin_cleanup();
  TFSingleton::destroy();
}

void* grk_thread_pool(void)
{
  return &TFSingleton::get();
}

size_t grk_num_workers(void)
{
  return TFSingleton::num_threads();
}

uint32_t grk_worker_id(void)
{
  return TFSingleton::workerId();
}

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

// Plugin loading is suppressed when the GRK_NO_PLUGIN environment variable is
// set (to any value). Cached on first read so the env is consulted once per
// process — useful for `make test` and similar contexts that want to force
// the CPU codec without changing call sites.
static bool grk_plugin_load_inhibited(void)
{
  static const bool inhibited = std::getenv("GRK_NO_PLUGIN") != nullptr;
  return inhibited;
}

void grk_initialize(const char* pluginPath, uint32_t numThreads, bool* plugin_initialized)
{
  if(plugin_initialized)
    *plugin_initialized = false;
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
    TFSingleton::create(numThreads);

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

    // 2. try to load plugin (skipped when GRK_NO_PLUGIN is set)
    if(!initState_.pluginInitialized_ && !grk_plugin_load_inhibited())
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
  if(wrapper)
    wrapper->ref();

  return obj;
}
GRK_API void GRK_CALLCONV grk_object_unref(grk_object* obj)
{
  if(!obj)
    return;
  auto wrapper = (RefCounted*)obj->wrapper;
  if(!wrapper)
    return;
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

bool grk_detect_format(const char* file_path, GRK_CODEC_FORMAT* format)
{
  if(!file_path || !format)
    return false;

  *format = GRK_CODEC_UNK;

  FILE* f = fopen(file_path, "rb");
  if(!f)
    return false;

  uint8_t buf[GRK_JPEG_2000_NUM_IDENTIFIER_BYTES];
  size_t bytesRead = fread(buf, 1, GRK_JPEG_2000_NUM_IDENTIFIER_BYTES, f);
  fclose(f);

  if(bytesRead < GRK_JPEG_2000_NUM_IDENTIFIER_BYTES)
    return false;

  return detectFormat(buf, format);
}

grk_image* grk_image_new(uint16_t numcmpts, grk_image_comp* cmptparms, GRK_COLOR_SPACE clrspc,
                         bool alloc_data)
{
  return GrkImage::create(nullptr, numcmpts, cmptparms, clrspc, alloc_data);
}

grk_data_type grk_get_data_type(bool compress, uint8_t prec, bool is_mct, uint8_t qmfbid)
{
  // 16- vs 32-bit data-type selection, derived from BIBO (bounded-input
  // bounded-output) analysis of the inverse wavelet transform.  The int16 path
  // stores coefficients in a fixed-point representation, so it is only viable when
  // the sample precision plus the worst-case headroom the transform needs still
  // fits in the 16-bit container:
  //     recommended = prec + headroom;   use int16 iff recommended <= 16.
  //
  //   Reversible 5/3 (T.800 F.3.4): the lifting is exact integer arithmetic, so no
  //   fractional bits are required — the only headroom is BIBO dynamic-range growth.
  //   The 2D inverse 5/3 BIBO gain needs 4 bits, plus 1 more when the reversible
  //   colour transform (RCT) adds its extra bit of range; a further guard bit is
  //   reserved once the total already exceeds 16.  int16 is therefore bit-exact for
  //   5/3 up to prec 12 (non-MCT) / 11 (MCT).
  //
  //   Irreversible 9/7 (T.800 F.3.5): the transform is real-valued, so the int16
  //   fixed-point coefficients must ALSO carry fractional bits, or the per-step
  //   lifting rounding accumulates into visible error (it compounds across the
  //   ~10 lifting/scaling ops of a 2D level and across decomposition levels).
  //   The 8-bit headroom budgets ~3 bits for the 9/7 2D BIBO gain (intermediate
  //   lifting values exceed the reconstructed sample) and 5 bits of fractional
  //   precision, which is what keeps the accumulated rounding inside the T.803
  //   conformance tolerances.  So prec + 8 <= 16  =>  prec <= 8 uses int16; at
  //   9 bits the fractional margin drops to 4 bits and the deviation from float
  //   doubles, so 9-bit and up (incl. DCI) decode in 32-bit float — exact.
  //   See doc/16BitDWT.md and WaveletReverse97_16.cpp.
  int recommended;
  if(qmfbid == 1) // reversible 5/3
  {
    recommended = (int)prec + (is_mct ? 5 : 4);
    if(recommended > 16)
      ++recommended;
  }
  else // irreversible 9/7
  {
    // the fixed point forward 9/7 rounds at every lifting step
    if(compress)
      return GRK_INT_32;
    recommended = (int)prec + 8;
  }
  return recommended <= 16 ? GRK_INT_16 : GRK_INT_32;
}

grk_image_meta* grk_image_meta_new(void)
{
  return (grk_image_meta*)(new GrkImageMeta());
}

static bool resolve_meta_field(grk_image_meta* meta, const char* field, uint8_t*** buf_pp,
                               size_t** len_pp)
{
  if(!meta || !field)
    return false;
  if(strcmp(field, "geotiff") == 0)
  {
    *buf_pp = &meta->geotiff_buf;
    *len_pp = &meta->geotiff_len;
  }
  else if(strcmp(field, "ipr") == 0)
  {
    *buf_pp = &meta->ipr_data;
    *len_pp = &meta->ipr_len;
  }
  else if(strcmp(field, "xmp") == 0)
  {
    *buf_pp = &meta->xmp_buf;
    *len_pp = &meta->xmp_len;
  }
  else if(strcmp(field, "iptc") == 0)
  {
    *buf_pp = &meta->iptc_buf;
    *len_pp = &meta->iptc_len;
  }
  else if(strcmp(field, "exif") == 0)
  {
    *buf_pp = &meta->exif_buf;
    *len_pp = &meta->exif_len;
  }
  else if(strcmp(field, "xml") == 0)
  {
    *buf_pp = &meta->xml_buf;
    *len_pp = &meta->xml_len;
  }
  else
  {
    return false;
  }
  return true;
}

bool grk_image_meta_set_field(grk_image_meta* meta, const char* field, const uint8_t* data,
                              size_t len)
{
  uint8_t** buf_p = nullptr;
  size_t* len_p = nullptr;
  if(!resolve_meta_field(meta, field, &buf_p, &len_p))
    return false;

  delete[] *buf_p;
  *buf_p = nullptr;
  *len_p = 0;

  if(data && len > 0)
  {
    *buf_p = new(std::nothrow) uint8_t[len];
    if(!*buf_p)
      return false;
    memcpy(*buf_p, data, len);
    *len_p = len;
  }
  return true;
}

bool grk_image_meta_get_field(grk_image_meta* meta, const char* field, uint8_t** data, size_t* len)
{
  uint8_t** buf_p = nullptr;
  size_t* len_p = nullptr;
  if(!resolve_meta_field(meta, field, &buf_p, &len_p))
    return false;

  if(data)
    *data = *buf_p;
  if(len)
    *len = *len_p;
  return true;
}

bool grk_image_meta_set_asocs(grk_image_meta* meta, const grk_asoc* asocs, uint32_t num_asocs)
{
  if(!meta || (!asocs && num_asocs > 0))
    return false;

  // Free existing asoc data
  if(meta->asoc_boxes)
  {
    for(uint32_t i = 0; i < meta->num_asoc_boxes; ++i)
    {
      free((void*)meta->asoc_boxes[i].label);
      free(meta->asoc_boxes[i].xml);
    }
    free(meta->asoc_boxes);
    meta->asoc_boxes = nullptr;
    meta->num_asoc_boxes = 0;
  }

  if(num_asocs == 0)
    return true;

  meta->asoc_boxes = (grk_asoc*)calloc(num_asocs, sizeof(grk_asoc));
  if(!meta->asoc_boxes)
    return false;

  meta->num_asoc_boxes = num_asocs;
  for(uint32_t i = 0; i < num_asocs; ++i)
  {
    meta->asoc_boxes[i].level = asocs[i].level;
    if(asocs[i].label)
    {
      size_t label_len = strlen(asocs[i].label);
      char* label_copy = (char*)malloc(label_len + 1);
      if(!label_copy)
        return false;
      memcpy(label_copy, asocs[i].label, label_len + 1);
      meta->asoc_boxes[i].label = label_copy;
    }
    if(asocs[i].xml && asocs[i].xml_len > 0)
    {
      meta->asoc_boxes[i].xml = (uint8_t*)malloc(asocs[i].xml_len);
      if(!meta->asoc_boxes[i].xml)
        return false;
      memcpy(meta->asoc_boxes[i].xml, asocs[i].xml, asocs[i].xml_len);
      meta->asoc_boxes[i].xml_len = asocs[i].xml_len;
    }
  }
  return true;
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
  grk::IStream* stream = nullptr;
  try
  {
    stream = sg.create();
  }
  catch(const std::exception& e)
  {
    grklog.error("grk_decompress_init: failed to create stream: %s", e.what());
    return nullptr;
  }
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
  // a non-zero initial_offset shifts stream offsets away from file offsets,
  // so leave the path unset and mercury reads via the stream instead
  if(streamParams->initial_offset == 0)
    codecImpl->decompressor_->setInputFilePath(streamParams->file);
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
void grk_decompress_schedule_swath_copy(grk_object* codecWrapper, const grk_wait_swath* swath,
                                        grk_swath_buffer* buf)
{
  if(!codecWrapper)
    return;

  auto codec = Codec::getImpl(codecWrapper);
  if(!codec->decompressor_)
    return;
  codec->decompressor_->scheduleSwathCopy(swath, buf);
}
void grk_decompress_wait_swath_copy(grk_object* codecWrapper)
{
  if(!codecWrapper)
    return;

  auto codec = Codec::getImpl(codecWrapper);
  if(!codec->decompressor_)
    return;
  codec->decompressor_->waitSwathCopy();
}
void grk_copy_tile_to_swath(const grk_image* tile_img, const grk_swath_buffer* buf)
{
  hwy_copy_tile_to_swath(tile_img, buf);
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
  auto f = codec->queueDecompressTile(tile_index);
  return f.get();
}
uint32_t grk_decompress_num_samples(grk_object* codecWrapper)
{
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    return codec->decompressor_ ? codec->decompressor_->getNumSamples() : 0;
  }
  return 0;
}
bool grk_decompress_sample(grk_object* codecWrapper, uint32_t sample_index)
{
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    return codec->decompressor_ ? codec->decompressor_->decompressSample(sample_index) : false;
  }
  return false;
}
grk_image* grk_decompress_get_sample_image(grk_object* codecWrapper, uint32_t sample_index)
{
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    return codec->decompressor_ ? codec->decompressor_->getSampleImage(sample_index) : nullptr;
  }
  return nullptr;
}
grk_image* grk_decompress_get_sample_tile_image(grk_object* codecWrapper, uint32_t sample_index,
                                                uint16_t tile_index)
{
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    return codec->decompressor_ ? codec->decompressor_->getSampleTileImage(sample_index, tile_index)
                                : nullptr;
  }
  return nullptr;
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

void grk_decompress_set_band_callback(grk_object* codecWrapper, grk_io_band_callback callback,
                                      void* user_data)
{
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    if(codec->decompressor_)
      codec->decompressor_->setBandCallback(callback, user_data);
  }
}
bool grk_image_is_post_process_no_op(grk_image* image)
{
  if(!image)
    return true;
  return static_cast<GrkImage*>(image)->isPostProcessNoOp();
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
    case GRK_CODEC_MJ2:
      codec = new Codec(stream);
      codec->compressor_ = new FileFormatMJ2Compress(stream);
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
  parameters->write_sop = false;
  parameters->write_eph = false;
  parameters->max_layers_transcode = 0;
  parameters->max_res_transcode = 0;
  parameters->transcode_prog_order = GRK_PROG_UNKNOWN;
  parameters->device_id = 0;
  parameters->repeats = 1;
}
bool grk_apply_xyz_transform(grk_image* image)
{
  return grk::applyXYZTransform(image);
}
grk_object* grk_compress_init(grk_stream_params* streamParams, grk_cparameters* parameters,
                              grk_image* image)
{
  if(!parameters || !image)
    return nullptr;
  if(parameters->cod_format != GRK_FMT_J2K && parameters->cod_format != GRK_FMT_JP2 &&
     parameters->cod_format != GRK_FMT_MJ2)
  {
    grklog.error("Unknown stream format.");
    return nullptr;
  }
  StreamGenerator sg(streamParams);
  grk::IStream* stream = nullptr;
  try
  {
    stream = sg.create();
  }
  catch(const std::exception& e)
  {
    grklog.error("failed to create stream: %s", e.what());
    return nullptr;
  }
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
    case GRK_FMT_MJ2: /* Motion JPEG 2000 */
      codecWrapper = grk_compress_create(GRK_CODEC_MJ2, stream);
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
uint64_t grk_compress_frame(grk_object* codecWrapper, grk_image* image, grk_plugin_tile* tile)
{
  if(codecWrapper && image)
  {
    auto codec = Codec::getImpl(codecWrapper);
    return codec->compressor_ ? codec->compressor_->compressFrame((GrkImage*)image, tile) : 0;
  }
  return 0;
}
bool grk_compress_finish(grk_object* codecWrapper)
{
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    return codec->compressor_ ? codec->compressor_->finalize() : false;
  }
  return false;
}

uint64_t grk_compress_get_compressed_length(grk_object* codecWrapper)
{
  if(codecWrapper)
  {
    auto codec = Codec::getImpl(codecWrapper);
    if(codec->stream_)
      return codec->stream_->tell();
  }
  return 0;
}

uint64_t grk_transcode(grk_stream_params* srcStream, grk_stream_params* dstStream,
                       grk_cparameters* parameters, grk_image* image)
{
  if(!srcStream || !dstStream || !parameters || !image)
  {
    grklog.error("grk_transcode: null parameter(s)");
    return 0;
  }

  grk_initialize(nullptr, UINT32_MAX, nullptr);

  /* Force JP2 format and transcode mode */
  parameters->cod_format = GRK_FMT_JP2;
  parameters->transcode = true;
  parameters->transcode_src = *srcStream;

  /* Create the output stream */
  dstStream->is_read_stream = false;
  StreamGenerator dstSg(dstStream);
  grk::IStream* outStream = nullptr;
  try
  {
    outStream = dstSg.create();
  }
  catch(const std::exception& e)
  {
    grklog.error("grk_transcode: failed to create destination stream: %s", e.what());
    return 0;
  }
  if(!outStream)
  {
    grklog.error("grk_transcode: failed to create destination stream");
    return 0;
  }

  /* Create the JP2 compressor */
  grk_object* codecWrapper = grk_compress_create(GRK_CODEC_JP2, outStream);
  if(!codecWrapper)
  {
    grklog.error("grk_transcode: failed to create codec");
    delete outStream;
    return 0;
  }

  auto codec = Codec::getImpl(codecWrapper);
  auto compressor = dynamic_cast<FileFormatJP2Compress*>(codec->compressor_);
  if(!compressor)
  {
    grklog.error("grk_transcode: internal error — expected JP2 compressor");
    grk_object_unref(codecWrapper);
    return 0;
  }

  /* Initialize the compressor (transcode mode: skips codestream encoding setup) */
  if(!compressor->init(parameters, (GrkImage*)image))
  {
    grklog.error("grk_transcode: failed to initialize compressor");
    grk_object_unref(codecWrapper);
    return 0;
  }

  /* Write JP2 boxes to destination (signature, ftyp, jp2h, metadata, skip_jp2c) */
  if(!compressor->start())
  {
    grklog.error("grk_transcode: failed to start compressor");
    grk_object_unref(codecWrapper);
    return 0;
  }

  /* Copy the raw codestream from source and finalize */
  srcStream->is_read_stream = true;
  StreamGenerator srcSg(srcStream);
  grk::IStream* inStream = nullptr;
  try
  {
    inStream = srcSg.create();
  }
  catch(const std::exception& e)
  {
    grklog.error("grk_transcode: failed to open source stream: %s", e.what());
    grk_object_unref(codecWrapper);
    return 0;
  }
  if(!inStream)
  {
    grklog.error("grk_transcode: failed to open source stream");
    grk_object_unref(codecWrapper);
    return 0;
  }
  std::unique_ptr<grk::IStream> srcStreamGuard(inStream);

  uint64_t bytesWritten = compressor->transcode(inStream);
  grk_object_unref(codecWrapper);

  return bytesWritten;
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
static bool pluginInitialized = false;
static bool pluginEnabled = true;
static std::atomic<uint64_t> pluginAcceleratedFrames{0};
static std::mutex pluginFrameMutex_;
bool grk_plugin_load(grk_plugin_load_info info)
{
  // form plugin name
  std::string pluginName = "";
#if !defined(_WIN32)
  pluginName += "lib";
#endif
  pluginName += std::string(GROK_PLUGIN_NAME) + "." + minpf_get_dynamic_library_extension();

  // Try explicit plugin path first, then the directory GRK_PLUGIN_PATH names
  const char* pluginDir =
      (info.pluginPath && info.pluginPath[0]) ? info.pluginPath : std::getenv("GRK_PLUGIN_PATH");
  if(pluginDir)
  {
    auto pluginPath = std::string(pluginDir) +
                      static_cast<char>(std::filesystem::path::preferred_separator) + pluginName;
    Logger::logger_.info("[plugin] Attempting to load plugin from '%s'", pluginPath.c_str());
    int32_t rc = minpf_load_from_path(pluginPath.c_str(), nullptr);
    if(!rc)
    {
      pluginLoaded = true;
      Logger::logger_.info("[plugin] Successfully loaded plugin '%s'", pluginName.c_str());
      return true;
    }
  }

  // Fallback: try current working directory (dcpomatic launches grk_compress
  // from its own binary dir without passing -g)
  std::string localPlugin =
      std::string(".") + (char)std::filesystem::path::preferred_separator + pluginName;
  Logger::logger_.info("[plugin] Trying local path '%s'", localPlugin.c_str());
  int32_t rc = minpf_load_from_path(localPlugin.c_str(), nullptr);
  if(!rc)
  {
    pluginLoaded = true;
    Logger::logger_.info("[plugin] Successfully loaded plugin '%s'", pluginName.c_str());
    return true;
  }

  // Fallback: try executable's directory
  std::error_code ec;
  auto exePath = std::filesystem::read_symlink("/proc/self/exe", ec);
  if(!ec)
  {
    auto exeDir = exePath.parent_path().string();
    auto exeDirPlugin =
        exeDir + static_cast<char>(std::filesystem::path::preferred_separator) + pluginName;
    Logger::logger_.info("[plugin] Trying executable dir '%s'", exeDirPlugin.c_str());
    rc = minpf_load_from_path(exeDirPlugin.c_str(), nullptr);
    if(!rc)
    {
      pluginLoaded = true;
      Logger::logger_.info("[plugin] Successfully loaded plugin '%s'", pluginName.c_str());
      return true;
    }
  }

  minpf_cleanup_plugin_manager();
  return false;
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
  pluginInitialized = false;
}
GRK_API void GRK_CALLCONV grk_plugin_set_enabled(bool enabled)
{
  pluginEnabled = enabled;
}
GRK_API uint64_t GRK_CALLCONV grk_plugin_accelerated_frames(void)
{
  return pluginAcceleratedFrames.load();
}
namespace grk
{
bool pluginAccelerates(void)
{
  return pluginLoaded && pluginInitialized && pluginEnabled;
}
std::mutex& pluginFrameMutex(void)
{
  return pluginFrameMutex_;
}
void pluginCountAcceleratedFrame(void)
{
  pluginAcceleratedFrames.fetch_add(1);
}
static const char* gpup_encode_mem_method_name = "gpup_encode_mem";
static const char* gpup_tile_free_method_name = "gpup_tile_free";
typedef int32_t (*GPUP_ENCODE_MEM)(gpup_compress_params* compress_parameters, gpup_image* image,
                                   gpup_tile** out_tile);
typedef void (*GPUP_TILE_FREE)(gpup_tile* tile);

int32_t pluginEncodeImage(const grk_cparameters* parameters, grk_image* image,
                          grk_plugin_tile** tile, void** rawTile)
{
  *tile = nullptr;
  *rawTile = nullptr;
  auto mgr = minpf_get_plugin_manager();
  if(!pluginLoaded || !mgr || mgr->num_libraries == 0)
    return 1;
  auto encode =
      (GPUP_ENCODE_MEM)minpf_get_symbol(mgr->dynamic_libraries[0], gpup_encode_mem_method_name);
  if(!encode)
  {
    Logger::logger_.warn("[plugin] Plugin missing '%s' symbol, compressing on the CPU",
                         gpup_encode_mem_method_name);
    return 1;
  }
  gpup_compress_params gpupParameters;
  grk_to_gpup_compress_params(parameters, &gpupParameters);
  auto gpupImage = grk_to_gpup_image(image);
  gpup_tile* gpupTile = nullptr;
  int32_t rc = encode(&gpupParameters, gpupImage, &gpupTile);
  gpup_image_free_shell(gpupImage);
  if(rc != 0)
    return rc < 0 ? -1 : 1;
  *tile = gpup_tile_to_grk(gpupTile);
  *rawTile = gpupTile;
  return 0;
}
void pluginReleaseEncodedTile(grk_plugin_tile* tile, void* rawTile)
{
  grk_plugin_tile_free_wrapper(tile);
  if(!rawTile)
    return;
  auto mgr = minpf_get_plugin_manager();
  if(!mgr || mgr->num_libraries == 0)
    return;
  auto freeTile =
      (GPUP_TILE_FREE)minpf_get_symbol(mgr->dynamic_libraries[0], gpup_tile_free_method_name);
  if(freeTile)
    freeTile((gpup_tile*)rawTile);
}
} // namespace grk
GRK_API bool GRK_CALLCONV grk_plugin_init(grk_plugin_init_info initInfo)
{
  if(!pluginLoaded)
  {
    return false;
  }
  auto mgr = minpf_get_plugin_manager();
  if(mgr && mgr->num_libraries > 0)
  {
    auto func = (PLUGIN_INIT)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_init_method_name);
    if(func)
    {
      gpup_init_info gpup_info;
      gpup_info.deviceId = initInfo.device_id;
      gpup_info.verbose = initInfo.verbose;
      gpup_info.license = initInfo.license;
      gpup_info.server = initInfo.server;
      bool result = func(gpup_info);
      if(!result)
        Logger::logger_.info("[plugin] Plugin init failed (device_id=%u, license='%s')",
                             initInfo.device_id, initInfo.license ? initInfo.license : "");
      pluginInitialized = result;
      return result;
    }
    Logger::logger_.info("[plugin] Plugin missing '%s' symbol", plugin_init_method_name);
  }
  return false;
}

/*******************
 Encode Implementation
 ********************/

GRK_PLUGIN_COMPRESS_USER_CALLBACK userEncodeCallback = 0;
static grk_cparameters* s_originalCompressParams = nullptr;

static const uint16_t maxWrappedComponents = 16;

/* the wrapper tree is deep, so keep it and re-point it when the batch pool
   hands back the same gpup_tile with new data */
static grk_plugin_tile* wrapPluginTile(gpup_tile* tile)
{
  static thread_local grk_plugin_tile* cachedWrapper = nullptr;
  static thread_local const gpup_tile* cachedSource = nullptr;
  if(tile != cachedSource)
  {
    grk_plugin_tile_free_wrapper(cachedWrapper);
    cachedWrapper = gpup_tile_to_grk(tile);
    cachedSource = tile;
  }
  else if(tile && cachedWrapper)
  {
    gpup_tile_update_grk(cachedWrapper, tile);
  }
  return cachedWrapper;
}

/* shallow wrapper: image and comps are the caller's storage, sample data is shared */
static void wrapPluginImage(const gpup_image* source, grk_image* image, grk_image_comp* comps)
{
  memset(image, 0, sizeof(*image));
  memset(comps, 0, sizeof(grk_image_comp) * maxWrappedComponents);
  image->x0 = source->x0;
  image->y0 = source->y0;
  image->x1 = source->x1;
  image->y1 = source->y1;
  image->numcomps = source->numcomps;
  image->color_space = (GRK_COLOR_SPACE)source->color_space;
  uint16_t numComponents =
      source->numcomps > maxWrappedComponents ? maxWrappedComponents : source->numcomps;
  for(uint16_t c = 0; c < numComponents; ++c)
  {
    comps[c].x0 = source->comps[c].x0;
    comps[c].y0 = source->comps[c].y0;
    comps[c].w = source->comps[c].w;
    comps[c].stride = source->comps[c].stride;
    comps[c].h = source->comps[c].h;
    comps[c].dx = source->comps[c].dx;
    comps[c].dy = source->comps[c].dy;
    comps[c].prec = source->comps[c].prec;
    comps[c].sgnd = source->comps[c].sgnd;
    comps[c].data = source->comps[c].data;
  }
  image->comps = comps;
}

/* bridge: receives gpup_ callback info from plugin, translates to grk_ for host callback */
uint64_t grk_plugin_internal_encode_callback(gpup_compress_callback_info* info)
{
  uint64_t rc = 0;
  if(!userEncodeCallback)
    return rc;

  grk_plugin_compress_user_callback_info grk_info;
  memset(&grk_info, 0, sizeof(grk_info));
  grk_info.input_file_name = info->input_file_name;
  grk_info.output_file_name_is_relative = info->outputFileNameIsRelative;
  grk_info.output_file_name = info->output_file_name;
  grk_info.compressor_parameters = s_originalCompressParams;
  grk_info.tile = wrapPluginTile(info->tile);
  grk_info.error_code = info->error_code;
  gpup_to_grk_stream_params(&info->stream_params, &grk_info.stream_params);

  grk_image grk_img;
  grk_image_comp grk_comps_buf[maxWrappedComponents];
  if(info->image)
  {
    wrapPluginImage(info->image, &grk_img, grk_comps_buf);
    grk_info.image = &grk_img;
  }

  rc = userEncodeCallback(&grk_info);
  return rc;
}
int32_t grk_plugin_compress(grk_cparameters* compress_parameters,
                            GRK_PLUGIN_COMPRESS_USER_CALLBACK callback)
{
  if(!pluginLoaded)
    return -1;
  userEncodeCallback = callback;
  s_originalCompressParams = compress_parameters;
  auto mgr = minpf_get_plugin_manager();
  if(mgr && mgr->num_libraries > 0)
  {
    auto func =
        (PLUGIN_ENCODE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_encode_method_name);
    if(func)
    {
      gpup_compress_params gpup_params;
      grk_to_gpup_compress_params(compress_parameters, &gpup_params);
      return func(&gpup_params, grk_plugin_internal_encode_callback);
    }
  }
  return -1;
}
int32_t grk_plugin_batch_compress(grk_plugin_compress_batch_info info)
{
  if(!pluginLoaded)
    return -1;
  userEncodeCallback = info.callback;
  s_originalCompressParams = info.compress_parameters;
  auto mgr = minpf_get_plugin_manager();
  if(mgr && mgr->num_libraries > 0)
  {
    auto func = (PLUGIN_BATCH_ENCODE)minpf_get_symbol(mgr->dynamic_libraries[0],
                                                      plugin_batch_encode_method_name);
    if(func)
    {
      gpup_compress_params gpup_params;
      grk_to_gpup_compress_params(info.compress_parameters, &gpup_params);
      gpup_compress_batch_info gpup_info;
      gpup_info.input_dir = info.input_dir;
      gpup_info.output_dir = info.output_dir;
      gpup_info.compress_parameters = &gpup_params;
      gpup_info.callback = grk_plugin_internal_encode_callback;
      return func(gpup_info);
    }
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
 In-memory batch compress
 ********************/

static const char* gpup_batch_memory_begin_method_name = "gpup_batch_memory_begin";
static const char* gpup_batch_memory_submit_method_name = "gpup_batch_memory_submit";
static const char* gpup_batch_memory_submit_planes_method_name =
    "gpup_batch_memory_submit_planes";
static const char* gpup_batch_memory_end_method_name = "gpup_batch_memory_end";
typedef int32_t (*GPUP_BATCH_MEMORY_BEGIN)(gpup_batch_memory_info* info);
typedef bool (*GPUP_BATCH_MEMORY_SUBMIT)(const uint8_t* packed, void* host_data);
typedef bool (*GPUP_BATCH_MEMORY_SUBMIT_PLANES)(const uint8_t* const planes[3],
                                                const size_t stride_bytes[3], void* host_data);
typedef bool (*GPUP_BATCH_MEMORY_END)(void);

namespace
{
struct BatchMemoryState
{
  bool running = false;
  uint32_t width = 0;
  uint32_t height = 0;
  uint16_t numcomps = 0;
  uint8_t prec = 0;
  uint8_t sourcePrec = 0;
  bool applyXYZ = false;
  // the plugin's preprocess kernel runs the colour transform, so submit leaves the
  // frame alone and packs it at sourcePrec
  bool xyzOnDevice = false;
  GRK_SOURCE_FORMAT sourceFormat = GRK_SOURCE_PLANAR_RGB;
  uint16_t rsiz = 0;
  // T2 parameters: the colour transform already ran on the submitted frame
  grk_cparameters t2Parameters = {};
  GRK_PLUGIN_BATCH_FRAME_CALLBACK callback = nullptr;
  void* user = nullptr;
};
BatchMemoryState batchMemory;

// the bound grk_compress uses for a compressed frame
size_t codestreamBound(const BatchMemoryState& state)
{
  return ((size_t)state.width * state.height * state.numcomps * ((state.prec + 7U) / 8U) * 3U) / 2U;
}

gpup_source_format toGpupSourceFormat(GRK_SOURCE_FORMAT format)
{
  switch(format)
  {
    case GRK_SOURCE_YUV420P:
      return GPUP_SOURCE_YUV420P;
    case GRK_SOURCE_YUV422P:
      return GPUP_SOURCE_YUV422P;
    case GRK_SOURCE_RGB48LE:
      return GPUP_SOURCE_RGB48LE;
    default:
      return GPUP_SOURCE_PLANAR_RGB;
  }
}

gpup_yuv_matrix toGpupYuvMatrix(GRK_YUV_MATRIX matrix)
{
  switch(matrix)
  {
    case GRK_YUV_BT601:
      return GPUP_YUV_BT601;
    case GRK_YUV_BT2020:
      return GPUP_YUV_BT2020;
    default:
      return GPUP_YUV_BT709;
  }
}

void* batchMemorySymbol(const char* name)
{
  auto mgr = minpf_get_plugin_manager();
  if(!pluginLoaded || !pluginInitialized || !mgr || mgr->num_libraries == 0)
    return nullptr;
  auto symbol = minpf_get_symbol(mgr->dynamic_libraries[0], name);
  if(!symbol)
    Logger::logger_.warn("[plugin] Plugin missing '%s' symbol", name);
  return symbol;
}
} // namespace

/* runs T2 on one frame's code blocks, on whichever plugin thread finished it */
static uint64_t batchMemoryEncodeCallback(gpup_compress_callback_info* info)
{
  if(!batchMemory.callback)
    return 0;
  // callbacks run concurrently, so each thread compresses into its own buffer
  static thread_local std::vector<uint8_t> codestream;
  auto bound = codestreamBound(batchMemory);
  if(codestream.size() < bound)
    codestream.resize(bound);

  uint64_t length = 0;
  if(info->image && info->tile)
  {
    grk_image headerImage;
    grk_image_comp headerComponents[maxWrappedComponents];
    wrapPluginImage(info->image, &headerImage, headerComponents);
    grk_stream_params stream = {};
    stream.buf = codestream.data();
    stream.buf_len = codestream.size();
    grk_cparameters parameters = batchMemory.t2Parameters;
    auto codec = grk_compress_init(&stream, &parameters, &headerImage);
    if(codec)
      length = grk_compress(codec, wrapPluginTile(info->tile));
    grk_object_unref(codec);
  }
  batchMemory.callback(batchMemory.user, info->host_data, codestream.data(), length);

  return length;
}

GRK_API int32_t GRK_CALLCONV grk_plugin_batch_memory_begin(grk_plugin_batch_memory_info info)
{
  if(batchMemory.running)
    return -1;
  if(!info.compress_parameters || !info.callback || !info.width || !info.height || !info.numcomps ||
     !info.prec)
    return -1;
  auto begin = (GPUP_BATCH_MEMORY_BEGIN)batchMemorySymbol(gpup_batch_memory_begin_method_name);
  if(!begin)
    return 1;
  if(info.source_format != GRK_SOURCE_PLANAR_RGB &&
     !batchMemorySymbol(gpup_batch_memory_submit_planes_method_name))
    return 1;

  gpup_compress_params gpupParameters;
  grk_to_gpup_compress_params(info.compress_parameters, &gpupParameters);
  if(gpupParameters.mct == 255)
    gpupParameters.mct = info.numcomps >= 3 ? 1 : 0;

  batchMemory.width = info.width;
  batchMemory.height = info.height;
  batchMemory.numcomps = info.numcomps;
  batchMemory.prec = info.prec;
  batchMemory.sourcePrec = info.source_prec ? info.source_prec : info.prec;
  batchMemory.applyXYZ = info.compress_parameters->apply_xyz_transform;
  batchMemory.xyzOnDevice = false;
  batchMemory.rsiz = info.compress_parameters->rsiz;
  batchMemory.t2Parameters = *info.compress_parameters;
  batchMemory.t2Parameters.apply_xyz_transform = false;
  batchMemory.t2Parameters.mct = gpupParameters.mct;
  batchMemory.callback = info.callback;
  batchMemory.user = info.user;
  batchMemory.sourceFormat = info.source_format;

  gpup_batch_memory_info gpupInfo;
  gpupInfo.compress_parameters = &gpupParameters;
  gpupInfo.width = info.width;
  gpupInfo.height = info.height;
  gpupInfo.numcomps = info.numcomps;
  gpupInfo.source_prec = batchMemory.sourcePrec;
  gpupInfo.prec = info.prec;
  gpupInfo.callback = batchMemoryEncodeCallback;
  gpupInfo.xyz_on_device = false;
  gpupInfo.source_format = toGpupSourceFormat(info.source_format);
  gpupInfo.yuv_matrix = toGpupYuvMatrix(info.yuv_matrix);
  gpupInfo.yuv_full_range = info.yuv_full_range;
  int32_t rc = begin(&gpupInfo);
  batchMemory.running = (rc == 0);
  batchMemory.xyzOnDevice = batchMemory.running && gpupInfo.xyz_on_device;
  if(info.xyz_on_device)
    *info.xyz_on_device = batchMemory.xyzOnDevice;

  return rc;
}

namespace
{
// the planar YUV shape the batch declared, exactly: grok hands the planes over
// without touching a sample, so anything else is a different picture
bool planarYuvPlanes(const grk_image* frame, const uint8_t* planes[3], size_t strideBytes[3])
{
  bool chromaIsHalfHeight = batchMemory.sourceFormat == GRK_SOURCE_YUV420P;
  size_t containerBytes = batchMemory.sourcePrec > 8 ? 2 : 1;
  auto expectedType = containerBytes == 2 ? GRK_INT_16 : GRK_INT_8;
  if(frame->numcomps != 3)
    return false;
  for(uint16_t c = 0; c < 3; ++c)
  {
    auto comp = frame->comps + c;
    bool isChroma = c > 0;
    uint32_t expectedWidth = isChroma ? (batchMemory.width + 1) / 2 : batchMemory.width;
    uint32_t expectedHeight = (isChroma && chromaIsHalfHeight) ? (batchMemory.height + 1) / 2
                                                               : batchMemory.height;
    uint8_t expectedSubsamplingX = isChroma ? 2 : 1;
    uint8_t expectedSubsamplingY = (isChroma && chromaIsHalfHeight) ? 2 : 1;
    if(!comp->data || comp->data_type != expectedType || comp->w != expectedWidth ||
       comp->h != expectedHeight || comp->stride < comp->w || comp->dx != expectedSubsamplingX ||
       comp->dy != expectedSubsamplingY || comp->prec != batchMemory.sourcePrec || comp->sgnd)
      return false;
    planes[c] = (const uint8_t*)comp->data;
    strideBytes[c] = (size_t)comp->stride * containerBytes;
  }

  return true;
}

// one interleaved 16 bit buffer, the layout the packed submit builds by hand
bool interleaved16Buffer(const grk_image* frame, const uint8_t* planes[3], size_t strideBytes[3])
{
  if(frame->numcomps != 3)
    return false;
  for(uint16_t c = 0; c < 3; ++c)
  {
    auto comp = frame->comps + c;
    if(comp->data_type != GRK_INT_16 || comp->w != batchMemory.width ||
       comp->h != batchMemory.height || comp->dx != 1 || comp->dy != 1 ||
       comp->prec != batchMemory.sourcePrec || comp->sgnd)
      return false;
  }
  auto first = frame->comps;
  if(!first->data || first->stride < (size_t)batchMemory.width * 3)
    return false;
  planes[0] = (const uint8_t*)first->data;
  strideBytes[0] = (size_t)first->stride * sizeof(uint16_t);

  return true;
}
} // namespace

GRK_API bool GRK_CALLCONV grk_plugin_batch_memory_submit(grk_image* frame, void* frame_user)
{
  if(!batchMemory.running || !frame || !frame->comps)
    return false;
  if(batchMemory.sourceFormat != GRK_SOURCE_PLANAR_RGB)
  {
    const uint8_t* planes[3] = {};
    size_t strideBytes[3] = {};
    bool described = batchMemory.sourceFormat == GRK_SOURCE_RGB48LE
                         ? interleaved16Buffer(frame, planes, strideBytes)
                         : planarYuvPlanes(frame, planes, strideBytes);
    if(!described)
      return false;
    auto submitPlanes = (GPUP_BATCH_MEMORY_SUBMIT_PLANES)batchMemorySymbol(
        gpup_batch_memory_submit_planes_method_name);
    if(!submitPlanes)
      return false;

    return submitPlanes(planes, strideBytes, frame_user);
  }
  if(frame->numcomps != batchMemory.numcomps)
    return false;
  for(uint16_t c = 0; c < frame->numcomps; ++c)
  {
    auto comp = frame->comps + c;
    if(comp->w != batchMemory.width || comp->h != batchMemory.height || !comp->data ||
       comp->data_type != GRK_INT_32)
      return false;
  }
  auto submit = (GPUP_BATCH_MEMORY_SUBMIT)batchMemorySymbol(gpup_batch_memory_submit_method_name);
  if(!submit)
    return false;
  if(batchMemory.xyzOnDevice)
  {
    // the kernel reads the source at its declared depth, so anything else is a
    // different transform
    for(uint16_t c = 0; c < frame->numcomps; ++c)
    {
      if(frame->comps[c].prec != batchMemory.sourcePrec)
        return false;
    }
  }
  else if(batchMemory.applyXYZ && !applyXYZTransform(frame, xyzTargetPrecision(batchMemory.rsiz)))
  {
    return false;
  }

  // pixel interleaved, little endian 16 bit, the packing the plugin reads.
  // The buffer is per thread and kept between frames: submit may be called from
  // several threads and the plugin copies before it returns.
  static thread_local std::vector<uint8_t> packed;
  size_t packedSize =
      (size_t)batchMemory.width * batchMemory.height * batchMemory.numcomps * sizeof(uint16_t);
  if(packed.size() != packedSize)
    packed.resize(packedSize);
  const size_t pixelStride = (size_t)batchMemory.numcomps * sizeof(uint16_t);
  for(uint16_t c = 0; c < frame->numcomps; ++c)
  {
    auto comp = frame->comps + c;
    uint8_t packedPrec = batchMemory.xyzOnDevice ? batchMemory.sourcePrec : batchMemory.prec;
    uint8_t shift = comp->prec > packedPrec ? (uint8_t)(comp->prec - packedPrec) : 0;
    auto data = (const int32_t*)comp->data;
    auto out = packed.data() + c * sizeof(uint16_t);
    for(uint32_t y = 0; y < batchMemory.height; ++y)
    {
      auto row = data + (size_t)y * comp->stride;
      for(uint32_t x = 0; x < batchMemory.width; ++x)
      {
        auto sample = (uint32_t)(row[x] >> shift);
        out[0] = (uint8_t)(sample & 0xFF);
        out[1] = (uint8_t)((sample >> 8) & 0xFF);
        out += pixelStride;
      }
    }
  }

  return submit(packed.data(), frame_user);
}

GRK_API bool GRK_CALLCONV grk_plugin_batch_memory_end(void)
{
  if(!batchMemory.running)
    return false;
  batchMemory.running = false;
  auto end = (GPUP_BATCH_MEMORY_END)batchMemorySymbol(gpup_batch_memory_end_method_name);
  if(!end)
    return false;

  return end();
}

/*******************
 Decompress Implementation
 ********************/

grk_plugin_decompress_callback decodeCallback = 0;
static grk_decompress_parameters* s_originalDecompressParams = nullptr;
static thread_local GPUP_INIT_DECOMPRESSORS s_gpupInitDecompressorsFn = nullptr;

/*
 * Bridge: grok's decompress_callback calls init_decompressors_func with grk_ types,
 * but the actual function (from the plugin) expects gpup_ types.
 */
static int grk_to_gpup_init_decompressors_bridge(grk_header_info* hdr, grk_image* img)
{
  if(!s_gpupInitDecompressorsFn)
    return -1;
  gpup_header_info gpup_hdr;
  grk_to_gpup_header_info(hdr, &gpup_hdr);
  gpup_image* gpup_img = grk_to_gpup_image(img);
  int rc = s_gpupInitDecompressorsFn(&gpup_hdr, gpup_img);
  gpup_to_grk_header_info(&gpup_hdr, hdr);
  gpup_image_free_shell(gpup_img);
  return rc;
}

/* bridge: receives gpup-typed PluginDecodeCallbackInfo from plugin, translates to grk_ for host */
int32_t grk_plugin_internal_decode_callback(PluginDecodeCallbackInfo* info)
{
  int32_t rc = -1;
  grk_plugin_decompress_callback_info grokInfo;
  memset(&grokInfo, 0, sizeof(grk_plugin_decompress_callback_info));

  // Bridge init_decompressors_func: plugin set it to a gpup_ function,
  // but grok's callback will call it with grk_ types
  if(info->init_decompressors_func)
  {
    s_gpupInitDecompressorsFn = info->init_decompressors_func;
    grokInfo.init_decompressors_func = grk_to_gpup_init_decompressors_bridge;
  }
  else
  {
    grokInfo.init_decompressors_func = nullptr;
  }
  grokInfo.input_file_name = info->inputFile.empty() ? nullptr : info->inputFile.c_str();
  grokInfo.output_file_name = info->outputFile.empty() ? nullptr : info->outputFile.c_str();
  grokInfo.decod_format = (GRK_CODEC_FORMAT)info->decod_format;
  grokInfo.cod_format = (GRK_SUPPORTED_FILE_FMT)info->cod_format;
  grokInfo.decompressor_parameters = s_originalDecompressParams;
  grokInfo.codec = (grk_object*)info->codec;
  grokInfo.plugin_owns_image = info->plugin_owns_image;

  // Cache the tile wrapper to avoid repeated deep alloc/free on every callback
  // (the plugin calls back multiple times per tile: HEADER, T2, POST_T1, CLEAN)
  static thread_local grk_plugin_tile* s_cachedTileWrapper = nullptr;
  static thread_local const gpup_tile* s_cachedTileSrc = nullptr;
  if(info->tile != s_cachedTileSrc)
  {
    if(s_cachedTileWrapper && info->tile)
    {
      // Reuse existing wrapper structure — just update data pointers
      gpup_tile_update_grk(s_cachedTileWrapper, info->tile);
    }
    else
    {
      grk_plugin_tile_free_wrapper(s_cachedTileWrapper);
      s_cachedTileWrapper = info->tile ? gpup_tile_to_grk(info->tile) : nullptr;
    }
    s_cachedTileSrc = info->tile;
  }
  grokInfo.tile = s_cachedTileWrapper;

  grokInfo.decompress_flags = info->decompress_flags;
  grokInfo.user_data =
      s_originalDecompressParams
          ? s_originalDecompressParams->user_data
          : (info->decompressor_parameters ? info->decompressor_parameters->user_data : nullptr);
  grokInfo.format_private = info->format_private;

  // header_info: translate gpup → grk
  gpup_to_grk_header_info(&info->header_info, &grokInfo.header_info);

  // image: wrap gpup_image as grk_image for the host callback
  grk_image grk_img;
  grk_image_comp grk_comps_buf[16];
  memset(&grk_img, 0, sizeof(grk_img));
  if(info->image)
  {
    grk_img.x0 = info->image->x0;
    grk_img.y0 = info->image->y0;
    grk_img.x1 = info->image->x1;
    grk_img.y1 = info->image->y1;
    grk_img.numcomps = info->image->numcomps;
    grk_img.color_space = (GRK_COLOR_SPACE)info->image->color_space;
    uint16_t nc = info->image->numcomps > 16 ? 16 : info->image->numcomps;
    memset(grk_comps_buf, 0, sizeof(grk_comps_buf));
    for(uint16_t c = 0; c < nc; ++c)
    {
      grk_comps_buf[c].x0 = info->image->comps[c].x0;
      grk_comps_buf[c].y0 = info->image->comps[c].y0;
      grk_comps_buf[c].w = info->image->comps[c].w;
      grk_comps_buf[c].stride = info->image->comps[c].stride;
      grk_comps_buf[c].h = info->image->comps[c].h;
      grk_comps_buf[c].dx = info->image->comps[c].dx;
      grk_comps_buf[c].dy = info->image->comps[c].dy;
      grk_comps_buf[c].prec = info->image->comps[c].prec;
      grk_comps_buf[c].sgnd = info->image->comps[c].sgnd;
      grk_comps_buf[c].data = info->image->comps[c].data;
    }
    grk_img.comps = grk_comps_buf;
    grk_img.decompress_num_comps = info->image->numcomps;
    grk_img.decompress_width = info->image->comps[0].w;
    grk_img.decompress_height = info->image->comps[0].h;
    grk_img.decompress_prec = info->image->comps[0].prec;
    grk_img.decompress_colour_space = (GRK_COLOR_SPACE)info->image->color_space;
    grokInfo.image = &grk_img;
  }

  if(decodeCallback)
    rc = decodeCallback(&grokInfo);

  // synch back: host callback may have created/modified image, codec, header_info
  if(grokInfo.image && grokInfo.image != &grk_img)
  {
    // Host created a new image (e.g. via grk_decompress_get_image during HEADER)
    gpup_image* gpupImg = grk_to_gpup_image(grokInfo.image);
    if(info->image)
      gpup_image_free_shell(info->image);
    info->image = gpupImg;
  }
  else if(!grokInfo.image && info->image)
  {
    gpup_image_free_shell(info->image);
    info->image = nullptr;
  }

  info->codec = (gpup_codec*)grokInfo.codec;
  grk_to_gpup_header_info(&grokInfo.header_info, &info->header_info);
  info->format_private = grokInfo.format_private;

  // After T2 callback: sync codeblock metadata from wrapper back to original gpup_tile
  // so that transferDecodeInfo() sees the updated num_passes, num_bit_planes, etc.
  if((info->decompress_flags & GPUP_DECODE_T2) && s_cachedTileWrapper && info->tile)
    grk_tile_sync_metadata_to_gpup(s_cachedTileWrapper, info->tile);

  // Free cached tile wrapper on CLEAN (final callback for this tile)
  if(info->decompress_flags & GRK_PLUGIN_DECODE_CLEAN)
  {
    grk_plugin_tile_free_wrapper(s_cachedTileWrapper);
    s_cachedTileWrapper = nullptr;
    s_cachedTileSrc = nullptr;
  }

  return rc;
}

int32_t grk_plugin_decompress(grk_decompress_parameters* decompress_parameters,
                              grk_plugin_decompress_callback callback)
{
  if(!pluginLoaded)
    return -1;
  decodeCallback = callback;
  s_originalDecompressParams = decompress_parameters;
  auto mgr = minpf_get_plugin_manager();
  if(mgr && mgr->num_libraries > 0)
  {
    auto func =
        (PLUGIN_DECODE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_decode_method_name);
    if(func)
    {
      gpup_decompress_params gpup_params;
      grk_to_gpup_decompress_params(decompress_parameters, &gpup_params);
      return func(&gpup_params, grk_plugin_internal_decode_callback);
    }
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
  s_originalDecompressParams = decompress_parameters;
  auto mgr = minpf_get_plugin_manager();
  if(mgr && mgr->num_libraries > 0)
  {
    auto func = (PLUGIN_INIT_BATCH_DECODE)minpf_get_symbol(mgr->dynamic_libraries[0],
                                                           plugin_init_batch_decode_method_name);
    if(func)
    {
      gpup_decompress_params gpup_params;
      grk_to_gpup_decompress_params(decompress_parameters, &gpup_params);
      return func(input_dir, output_dir, &gpup_params, grk_plugin_internal_decode_callback);
    }
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
