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

#pragma once

#include "grok.h" // for grk_msg_callback
#include "ILogger.h"

namespace grk
{

struct Logger : public ILogger
{
  Logger()
      : error_data_(nullptr), warning_data_(nullptr), info_data_(nullptr), debug_data_(nullptr),
        trace_data_(nullptr), error_handler(nullptr), warning_handler(nullptr),
        info_handler(nullptr), debug_handler(nullptr), trace_handler(nullptr)
  {}

  void info(const char* fmt, ...) override
  {
    if(!info_handler)
      return;
    va_list args;
    va_start(args, fmt);
    log_message(info_handler, info_data_, fmt, args);
    va_end(args);
  }

  void warn(const char* fmt, ...) override
  {
    if(!warning_handler)
      return;
    va_list args;
    va_start(args, fmt);
    log_message(warning_handler, warning_data_, fmt, args);
    va_end(args);
  }

  void error(const char* fmt, ...) override
  {
    if(!error_handler)
      return;
    va_list args;
    va_start(args, fmt);
    log_message(error_handler, error_data_, fmt, args);
    va_end(args);
  }

  void debug(const char* fmt, ...) override
  {
    if(!debug_handler)
      return;
    va_list args;
    va_start(args, fmt);
    log_message(debug_handler, debug_data_, fmt, args);
    va_end(args);
  }

  void trace(const char* fmt, ...) override
  {
    if(!trace_handler)
      return;
    va_list args;
    va_start(args, fmt);
    log_message(trace_handler, trace_data_, fmt, args);
    va_end(args);
  }

  void* error_data_;
  void* warning_data_;
  void* info_data_;
  void* debug_data_;
  void* trace_data_;
  grk_msg_callback error_handler;
  grk_msg_callback warning_handler;
  grk_msg_callback info_handler;
  grk_msg_callback debug_handler;
  grk_msg_callback trace_handler;

  static Logger logger_;

private:
  void log_message(grk_msg_callback msg_handler, void* l_data, const char* fmt,
                   va_list args) noexcept
  {
    constexpr int message_size = 512;
    if(fmt != nullptr)
    {
      char buffer[message_size];
      vsnprintf(buffer, message_size, fmt, args);
      msg_handler(buffer, l_data);
    }
  }
};

extern Logger& grklog;

} // namespace grk