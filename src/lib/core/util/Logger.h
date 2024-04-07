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
 */

#pragma once

#include <cstring>
#include <cstdarg>

#include "grok.h"
#include "ILogger.h"

namespace grk
{

struct Logger : public ILogger
{
   Logger()
	   : error_data_(nullptr), warning_data_(nullptr), info_data_(nullptr), error_handler(nullptr),
		 warning_handler(nullptr), info_handler(nullptr)
   {}

   void info(const char* fmt, ...) override
   {
	  if(!info_handler)
		 return;
	  va_list arg;
	  va_start(arg, fmt);
	  log_message(info_handler, info_data_, fmt, arg);
	  va_end(arg);
   }
   void warn(const char* fmt, ...) override
   {
	  if(!warning_handler)
		 return;
	  va_list arg;
	  va_start(arg, fmt);
	  log_message(warning_handler, warning_data_, fmt, arg);
	  va_end(arg);
   }
   void error(const char* fmt, ...) override
   {
	  if(!error_handler)
		 return;
	  va_list arg;
	  va_start(arg, fmt);
	  log_message(error_handler, error_data_, fmt, arg);
	  va_end(arg);
   }

   void* error_data_;
   void* warning_data_;
   void* info_data_;
   grk_msg_callback error_handler;
   grk_msg_callback warning_handler;
   grk_msg_callback info_handler;

   static Logger logger_;

 private:
   template<typename... Args>
   void log_message(grk_msg_callback msg_handler, void* l_data, char const* const format,
					Args&... args) noexcept
   {
	  const int message_size = 512;
	  if((format != nullptr))
	  {
		 char message[message_size];
		 memset(message, 0, message_size);
		 vsnprintf(message, message_size, format, args...);
		 msg_handler(message, l_data);
	  }
   }
};

} // namespace grk
