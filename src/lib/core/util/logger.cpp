/*
 *    Copyright (C) 2016-2023 Grok Image Compression Inc.
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
 */

#include "grk_includes.h"

namespace grk
{
logger logger::logger_;

logger::logger()
	: error_data_(nullptr), warning_data_(nullptr), info_data_(nullptr), error_handler(nullptr),
	  warning_handler(nullptr), info_handler(nullptr)
{}

template<typename... Args>
void log(grk_msg_callback msg_handler, void* l_data, char const* const format,
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

void GRK_INFO(const char* fmt, ...)
{
	if(!logger::logger_.info_handler)
		return;
	va_list arg;
	va_start(arg, fmt);
	log(logger::logger_.info_handler, logger::logger_.info_data_, fmt, arg);
	va_end(arg);
}
void GRK_WARN(const char* fmt, ...)
{
	if(!logger::logger_.warning_handler)
		return;
	va_list arg;
	va_start(arg, fmt);
	log(logger::logger_.warning_handler, logger::logger_.warning_data_, fmt, arg);
	va_end(arg);
}
void GRK_ERROR(const char* fmt, ...)
{
	if(!logger::logger_.error_handler)
		return;
	va_list arg;
	va_start(arg, fmt);
	log(logger::logger_.error_handler, logger::logger_.error_data_, fmt, arg);
	va_end(arg);
}

} // namespace grk
