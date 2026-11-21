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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "grok.h"

constexpr int expectedArgumentCount = 2;
constexpr const char* mercurySuccessMarker = "mercury fast path: decoded";
constexpr uint32_t decodeWindowSize = 1024;
static std::mutex logMutex;
static std::string logText;

static void appendLog(const char* message, void*)
{
  std::lock_guard<std::mutex> lock(logMutex);
  logText += message;
  logText += '\n';
}

int main(int argumentCount, char** argumentValues)
{
  if(argumentCount != expectedArgumentCount)
  {
    fprintf(stderr, "usage: %s <fuzz codestream>\n", argumentValues[0]);
    return 1;
  }

#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "1");
#else
  setenv("GRK_MERCURY", "1", 1);
#endif

  grk_msg_handlers handlers{};
  handlers.info_callback = appendLog;
  handlers.warn_callback = appendLog;
  handlers.error_callback = appendLog;
  grk_set_msg_handlers(handlers);
  grk_initialize(nullptr, 0, nullptr);

  grk_decompress_parameters parameters{};
  parameters.dw_x1 = decodeWindowSize;
  parameters.dw_y1 = decodeWindowSize;

  grk_stream_params streamParameters{};
  streamParameters.is_read_stream = true;
  snprintf(streamParameters.file, sizeof(streamParameters.file), "%s", argumentValues[1]);

  grk_object* codec = grk_decompress_init(&streamParameters, &parameters);
  grk_header_info headerInfo{};
  bool decoded =
      codec && grk_decompress_read_header(codec, &headerInfo) && grk_decompress(codec, nullptr);
  if(codec)
    grk_object_unref(codec);
  grk_deinitialize();

  if(!decoded)
  {
    fprintf(stderr, "decode failed\n%s", logText.c_str());
    return 1;
  }
  if(logText.find(mercurySuccessMarker) == std::string::npos)
  {
    fprintf(stderr, "mercury decode was not used\n%s", logText.c_str());
    return 1;
  }
  return 0;
}
