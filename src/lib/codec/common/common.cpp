/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#ifdef _MSC_VER
#include <intrin.h>
#define strcasecmp _stricmp
#endif

#include "spdlogwrapper.h"
#include <spdlog/sinks/basic_file_sink.h> // Required for basic_logger_mt
#include <spdlog/sinks/stdout_color_sinks.h>
#include "common.h"

namespace grk
{

ChronoTimer::ChronoTimer(const std::string& msg) : message(msg) {}
void ChronoTimer::start(void)
{
  startTime = std::chrono::high_resolution_clock::now();
}
void ChronoTimer::finish(void)
{
  auto finish = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = finish - startTime;
  spdlog::info("{} : {} ms", message, elapsed.count() * 1000);
}

bool validateDirectory(std::string dir)
{
  if(!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
  {
    spdlog::error("Directory {} does not exist or is not in fact a directory", dir);
    return false;
  }

  return true;
}
std::string convertFileFmtToString(GRK_SUPPORTED_FILE_FMT fmt)
{
  switch(fmt)
  {
    case GRK_FMT_J2K:
      return "J2K";
      break;
    case GRK_FMT_JP2:
      return "JP2";
      break;
    case GRK_FMT_PXM:
      return "PNM";
      break;
    case GRK_FMT_PGX:
      return "PGX";
      break;
    case GRK_FMT_PAM:
      return "PAM";
      break;
    case GRK_FMT_BMP:
      return "BMP";
      break;
    case GRK_FMT_TIF:
      return "TIFF";
      break;
    case GRK_FMT_RAW:
      return "RAW";
      break;
    case GRK_FMT_YUV:
      return "YUV";
      break;
    case GRK_FMT_PNG:
      return "PNG";
      break;
    case GRK_FMT_RAWL:
      return "RAWL";
      break;
    case GRK_FMT_JPG:
      return "JPEG";
      break;
    case GRK_FMT_UNK:
    default:
      return "UNKNOWN";
      break;
  }
}

bool parseWindowBounds(char* inArg, double* dw_x0, double* dw_y0, double* dw_x1, double* dw_y1)
{
  int it = 0;
  double val[4];
  const char delims[] = ",";
  const char* result = strtok(inArg, delims);

  while((result != nullptr) && (it < 4))
  {
    val[it] = atof(result);
    result = strtok(nullptr, delims);
    it++;
  }

  // region must be specified by 4 values exactly
  if(it != 4)
  {
    spdlog::warn("Decompress region must be specified by exactly "
                 "four coordinates. Ignoring specified region.");
    return false;
  }

  // don't allow negative values
  if((val[0] < 0 || val[1] < 0 || val[2] < 0 || val[3] < 0))
  {
    spdlog::warn("Decompress region cannot contain negative "
                 "values.\n Ignoring specified region ({},{},{},{}).",
                 val[0], val[1], val[2], val[3]);
    return false;
  }
  if(val[2] <= val[0] || val[3] <= val[1])
  {
    spdlog::warn("Decompress region must have strictly "
                 "positive area.\n Ignoring specified region ({},{},{},{}).",
                 val[0], val[1], val[2], val[3]);
    return false;
  }

  // sanity check
  bool allLessThanOne = true;
  for(uint8_t i = 0; i < 4; ++i)
  {
    if(val[i] > 1.0f)
      allLessThanOne = false;
  }
  // note: special case of [0,0,1,1] is interpreted as relative coordinates
  if(!allLessThanOne && (val[0] != 0 || val[1] != 0 || val[2] != 1 || val[3] != 1))
  {
    for(uint8_t i = 0; i < 4; ++i)
    {
      if(val[i] != (uint32_t)val[i])
      {
        spdlog::warn("Decompress region in absolute coordinates must only contain integers."
                     "\n Ignoring specified region ({},{},{},{}).",
                     val[0], val[1], val[2], val[3]);
        return false;
      }
    }
  }

  *dw_x0 = val[0];
  *dw_y0 = val[1];
  *dw_x1 = val[2];
  *dw_y1 = val[3];

  return true;
}

bool safe_fclose(FILE* file)
{
  if(!file)
    return true;
  return fclose(file) ? false : true;
}

bool useStdio(const std::string& filename)
{
  return filename.empty();
}

bool supportedStdioFormat(GRK_SUPPORTED_FILE_FMT format, bool compress)
{
  if(compress)
  {
    for(size_t i = 0;
        i < sizeof(supportedStdoutFileFormatsCompress) / sizeof(GRK_SUPPORTED_FILE_FMT); ++i)
      if(supportedStdoutFileFormatsCompress[i] == format)
        return true;
  }
  else
  {
    for(size_t i = 0;
        i < sizeof(supportedStdoutFileFormatsDecompress) / sizeof(GRK_SUPPORTED_FILE_FMT); ++i)
      if(supportedStdoutFileFormatsDecompress[i] == format)
        return true;
  }
  return false;
}

bool grk_set_binary_mode([[maybe_unused]] FILE* file)
{
#ifdef _WIN32
  return (_setmode(_fileno(file), _O_BINARY) != -1);
#else
  return true;
#endif
}

bool grk_open_for_output(FILE** fdest, const char* outfile, bool writeToStdout)
{
  assert(fdest);
  assert(!*fdest);
  if(writeToStdout)
  {
    if(!grk::grk_set_binary_mode(stdout))
      return false;
    *fdest = stdout;
  }
  else
  {
    *fdest = fopen(outfile, "wb");
    if(!*fdest)
    {
      spdlog::error("failed to open {} for writing", outfile);
      return false;
    }
  }
  return true;
}

GRK_SUPPORTED_FILE_FMT grk_get_file_format(const char* filename, bool& isHTJ2K)
{
  const char* ext = strrchr(filename, '.');
  isHTJ2K = false;
  if(ext == nullptr)
    return GRK_FMT_UNK;
  ext++;
  if(*ext)
  {
    static const std::unordered_map<std::string, GRK_SUPPORTED_FILE_FMT> extension_map = {
        {"pgx", GRK_FMT_PGX},   {"pam", GRK_FMT_PXM}, {"pnm", GRK_FMT_PXM},  {"pgm", GRK_FMT_PXM},
        {"ppm", GRK_FMT_PXM},   {"pbm", GRK_FMT_PXM}, {"bmp", GRK_FMT_BMP},  {"tif", GRK_FMT_TIF},
        {"tiff", GRK_FMT_TIF},  {"jpg", GRK_FMT_JPG}, {"jpeg", GRK_FMT_JPG}, {"raw", GRK_FMT_RAW},
        {"rawl", GRK_FMT_RAWL}, {"yuv", GRK_FMT_YUV}, {"png", GRK_FMT_PNG},  {"j2k", GRK_FMT_J2K},
        {"jp2", GRK_FMT_JP2},   {"j2c", GRK_FMT_J2K}, {"jpc", GRK_FMT_J2K},  {"jph", GRK_FMT_JP2},
        {"jhc", GRK_FMT_J2K}};

    static const std::unordered_set<std::string> htj2k_extensions = {"jph", "jhc"};

    auto it = extension_map.find(ext);
    if(it != extension_map.end())
    {
      if(htj2k_extensions.contains(ext))
      {
        isHTJ2K = true;
      }
      return it->second;
    }
  }

  return GRK_FMT_UNK;
}

GRK_SUPPORTED_FILE_FMT grk_get_file_format(const char* filename)
{
  bool isHTJ2K;
  return grk_get_file_format(filename, isHTJ2K);
}

bool isFinalOutputSubsampled(grk_image* image)
{
  assert(image);
  if(image->upsample || image->force_rgb)
    return false;
  for(uint16_t i = 0; i < image->numcomps; ++i)
  {
    if(image->comps[i].dx != 1 || image->comps[i].dy != 1)
      return true;
  }
  return false;
}

const char* pathSeparator()
{
#ifdef _WIN32
  return "\\";
#else
  return "/";
#endif
}

char* get_file_name(char* name)
{
  return strtok(name, ".");
}

uint32_t get_num_images(char* imgdirpath)
{
  auto count = std::count_if(std::filesystem::directory_iterator(imgdirpath),
                             std::filesystem::directory_iterator{},
                             [](const auto& entry) { return entry.is_regular_file(); });

  if(count > std::numeric_limits<uint32_t>::max())
  {
    throw std::overflow_error("Count exceeds uint32_t range");
  }

  return static_cast<uint32_t>(count);
}

char* actual_path(const char* outfile, bool* mem_allocated)
{
  if(mem_allocated)
    *mem_allocated = false;
  if(!outfile)
    return nullptr;
#ifdef _WIN32
  return (char*)outfile;
#else
  char* actualpath = realpath(outfile, nullptr);
  if(actualpath != nullptr)
  {
    if(mem_allocated)
      *mem_allocated = true;
    return actualpath;
  }
  return (char*)outfile;
#endif
}

uint32_t uint_adds(uint32_t a, uint32_t b)
{
  uint64_t sum = (uint64_t)a + (uint64_t)b;
  return (uint32_t)(-(int32_t)(sum >> 32)) | (uint32_t)sum;
}

int population_count(uint32_t val)
{
#ifdef _MSC_VER
  return __popcnt(val);
#elif (defined(__GNUC__) || defined(__clang__))
  return __builtin_popcount(val);
#else
  val -= ((val >> 1) & 0x55555555);
  val = (((val >> 2) & 0x33333333) + (val & 0x33333333));
  val = (((val >> 4) + val) & 0x0f0f0f0f);
  val += (val >> 8);
  val += (val >> 16);
  return (int)(val & 0x0000003f);
#endif
}

#ifdef _MSC_VER
#pragma intrinsic(_BitScanReverse)
#endif
int count_leading_zeros(uint32_t val)
{
#ifdef _MSC_VER
  unsigned long result = 0;
  _BitScanReverse(&result, val);
  return 31 ^ (int)result;
#elif (defined(__GNUC__) || defined(__clang__))
  return __builtin_clz(val);
#else
  val |= (val >> 1);
  val |= (val >> 2);
  val |= (val >> 4);
  val |= (val >> 8);
  val |= (val >> 16);
  return 32 - population_count(val);
#endif
}

#ifdef _MSC_VER
#pragma intrinsic(_BitScanForward)
#endif
int count_trailing_zeros(uint32_t val)
{
#ifdef _MSC_VER
  unsigned long result = 0;
  _BitScanForward(&result, val);
  return (int)result;
#elif (defined(__GNUC__) || defined(__clang__))
  return __builtin_ctz(val);
#else
  val |= (val << 1);
  val |= (val << 2);
  val |= (val << 4);
  val |= (val << 8);
  val |= (val << 16);
  return 32 - population_count(val);
#endif
}

void errorCallback(const char* msg, [[maybe_unused]] void* client_data)
{
  spdlog::default_logger()->error(msg);
}
void warningCallback(const char* msg, [[maybe_unused]] void* client_data)
{
  spdlog::default_logger()->warn(msg);
}
void infoCallback(const char* msg, [[maybe_unused]] void* client_data)
{
  spdlog::default_logger()->info(msg);
}
void debugCallback(const char* msg, [[maybe_unused]] void* client_data)
{
  spdlog::default_logger()->debug(msg);
}

void traceCallback(const char* msg, [[maybe_unused]] void* client_data)
{
  spdlog::default_logger()->trace(msg);
}

// Configuration function
void configureLogging(const std::string& logfile)
{
  // Step 1: Set up the logger
  std::shared_ptr<spdlog::logger> logger;
  if(!logfile.empty())
  {
    logger = spdlog::basic_logger_mt("grk", logfile); // File logger
  }
  else
  {
    // Console logger with color
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    logger = std::make_shared<spdlog::logger>("grk", sink);
  }
  spdlog::set_default_logger(logger);

  // Step 2: Determine log level
  spdlog::level::level_enum log_level = spdlog::level::err; // Default to errors only

  const char* debug_env = std::getenv("GRK_DEBUG");
  if(debug_env)
  {
    int level = std::atoi(debug_env);
    switch(level)
    {
      case 0:
        log_level = spdlog::level::off;
        break;
      case 1:
        log_level = spdlog::level::err;
        break;
      case 2:
        log_level = spdlog::level::warn;
        break;
      case 3:
        log_level = spdlog::level::info;
        break;
      case 4:
        log_level = spdlog::level::debug;
        break;
      case 5:
        log_level = spdlog::level::trace;
        break;
      default:
        if(level > 5)
          log_level = spdlog::level::trace; // Cap at trace
        break;
    }
  }
  else
  {
    log_level = spdlog::level::off;
  }
  spdlog::set_level(log_level);
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

  // Step 3: Set up grk_msg_handlers
  grk_msg_handlers handlers = {infoCallback,  nullptr, debugCallback,   nullptr,
                               traceCallback, nullptr, warningCallback, nullptr,
                               errorCallback, // Always active
                               nullptr};

  grk_set_msg_handlers(handlers);
}

} // namespace grk
