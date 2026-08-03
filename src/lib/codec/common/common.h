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

#pragma once

#include "grk_config_private.h"
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <cstring>
#include <fcntl.h>
#endif /* _WIN32 */
#include <bit>
#include <chrono>

#include "grok.h"
#include "FileOrchestratorIO.h"

#if defined(_WIN32)
#define GRK_FSEEK(stream, offset, whence) _fseeki64(stream, /* __int64 */ offset, whence)
#define GRK_FTELL(stream) /* __int64 */ _ftelli64(stream)
#else
/*
 Use fseeko() and ftello() if they are available since they use
 'int64_t' rather than 'long'.  It is wrong to use fseeko() and
 ftello() only on systems with special LFS support since some systems
 (e.g. FreeBSD) support a 64-bit int64_t by default.
 */
#if defined(GROK_HAVE_FSEEKO) && !defined(fseek)
#define fseek fseeko
#define ftell ftello
#endif
#define GRK_FSEEK(stream, offset, whence) fseek(stream, offset, whence)
#define GRK_FTELL(stream) ftell(stream)
#endif

namespace grk
{

enum GrkRC
{
  GrkRCSuccess,
  GrkRCFail,
  GrkRCParseArgsFailed,
  GrkRCUsage
};

template<typename... Args>
void log(grk_msg_callback msg_handler, void* data, char const* const format, Args&... args) noexcept
{
  const int message_size = 512;
  if((format != nullptr))
  {
    char message[message_size];
    memset(message, 0, message_size);
    vsnprintf(message, message_size, format, args...);
    msg_handler(message, data);
  }
}

const GRK_SUPPORTED_FILE_FMT supportedStdoutFileFormatsCompress[] = {
    GRK_FMT_PNG, GRK_FMT_PXM, GRK_FMT_RAW, GRK_FMT_RAWL, GRK_FMT_JPG};
const GRK_SUPPORTED_FILE_FMT supportedStdoutFileFormatsDecompress[] = {
    GRK_FMT_BMP, GRK_FMT_PNG, GRK_FMT_PXM, GRK_FMT_RAW, GRK_FMT_RAWL, GRK_FMT_JPG};

const size_t maxICCProfileBufferLen = 10000000;

template<size_t N>
void safe_strcpy(char (&dest)[N], const char* src)
{
  size_t len = strnlen(src, N - 1);
  memcpy(dest, src, len);
  dest[len] = '\0';
}

class ChronoTimer
{
public:
  explicit ChronoTimer(const std::string& msg);
  void start(void);
  void finish(void);

private:
  std::string message;
  std::chrono::high_resolution_clock::time_point startTime;
};

struct grk_img_fol
{
  char* imgdirpath;
  const char* out_format;
  bool set_imgdir;
  bool set_out_format;
};

bool validateDirectory(std::string dir);
std::string convertFileFmtToString(GRK_SUPPORTED_FILE_FMT fmt);
bool parseWindowBounds(char* inArg, double* dw_x0, double* dw_y0, double* dw_x1, double* dw_y1);
bool safe_fclose(FILE* fd);
bool useStdio(const std::string& filename);
bool supportedStdioFormat(GRK_SUPPORTED_FILE_FMT format, bool compress);
bool grk_open_for_output(FILE** fdest, const char* outfile, bool writeToStdout);
bool grk_set_binary_mode(FILE* file);
GRK_SUPPORTED_FILE_FMT grk_get_file_format(const char* filename, bool& isHTJ2K);
GRK_SUPPORTED_FILE_FMT grk_get_file_format(const char* filename);
const char* pathSeparator();
char* get_file_name(char* name);
uint32_t get_num_images(char* imgdirpath);
char* actual_path(const char* outfile, bool* mem_allocated);
bool isFinalOutputSubsampled(grk_image* image);

template<typename T>
inline T swap(T x)
{
  static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);
  if constexpr(sizeof(T) == 1)
    return x;
  else if constexpr(sizeof(T) == 2)
    return (T)std::byteswap((uint16_t)x);
  else if constexpr(sizeof(T) == 4)
    return (T)std::byteswap((uint32_t)x);
  else
    return (T)std::byteswap((uint64_t)x);
}
template<typename T>
inline T endian(T x, bool to_big_endian)
{
  if constexpr(std::endian::native == std::endian::big)
    return to_big_endian ? x : swap<T>(x);
  else
    return to_big_endian ? swap<T>(x) : x;
}

template<typename T>
uint32_t ceildiv(T a, T b)
{
  assert(b);
  return (uint32_t)((a + (uint64_t)b - 1) / b);
}

template<typename T>
inline bool writeBytes(T val, T* buf, T** outPtr, size_t* outCount, size_t len, bool big_endian,
                       FILE* out)
{
  if(*outCount >= len)
    return false;
  *(*outPtr)++ = grk::endian<T>(val, big_endian);
  (*outCount)++;
  if(*outCount == len)
  {
    size_t res = fwrite(buf, 1, sizeof(T) * len, out);
    if(res != sizeof(T) * len)
      return false;
    *outCount = 0;
    *outPtr = buf;
  }
  return true;
}

template<typename T>
inline bool writeBytes(T val, T* buf, T** outPtr, size_t* outCount, size_t len, bool big_endian,
                       FileOrchestratorIO* ser)
{
  if(*outCount >= len)
    return false;
  *(*outPtr)++ = grk::endian<T>(val, big_endian);
  (*outCount)++;
  if(*outCount == len)
  {
    if(!ser->write((uint8_t*)buf, sizeof(T) * len))
      return false;
    *outCount = 0;
    *outPtr = buf;
  }
  return true;
}

uint32_t uint_adds(uint32_t a, uint32_t b);
int population_count(uint32_t val);
int count_leading_zeros(uint32_t val);
int count_trailing_zeros(uint32_t val);

void infoCallback(const char* msg, void* client_data);
void debugCallback(const char* msg, void* client_data);
void traceCallback(const char* msg, void* client_data);
void warningCallback(const char* msg, void* client_data);
void errorCallback(const char* msg, void* client_data);

void configureLogging(const std::string& logfile);

} // namespace grk
