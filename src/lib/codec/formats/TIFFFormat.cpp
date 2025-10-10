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

#include "grk_apps_config.h"

#ifdef GROK_HAVE_LIBTIFF

#include "TIFFFormat.h"

static void tiff_error(const char* msg, [[maybe_unused]] void* client_data)
{
  if(msg)
  {
    std::string out = std::string("libtiff: ") + msg;
    spdlog::error(out);
  }
}

static bool tiffWarningHandlerVerbose = true;
void MyTiffErrorHandler([[maybe_unused]] const char* module, const char* fmt, va_list ap)
{
  grk::log(tiff_error, nullptr, fmt, ap);
}

static void tiff_warn(const char* msg, [[maybe_unused]] void* client_data)
{
  if(msg)
  {
    std::string out = std::string("libtiff: ") + msg;
    spdlog::warn(out);
  }
}

void MyTiffWarningHandler([[maybe_unused]] const char* module, const char* fmt, va_list ap)
{
  if(tiffWarningHandlerVerbose)
    grk::log(tiff_warn, nullptr, fmt, ap);
}

void tiffSetErrorAndWarningHandlers(bool verbose)
{
  tiffWarningHandlerVerbose = verbose;
  TIFFSetErrorHandler(MyTiffErrorHandler);
  TIFFSetWarningHandler(MyTiffWarningHandler);
}

#endif
