/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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

#include "grok_includes.h"

namespace grk {

logger logger::m_logger;

logger::logger() : m_error_data(nullptr),
					m_warning_data(nullptr),
					m_info_data(nullptr),
					error_handler(nullptr),
					warning_handler(nullptr),
					info_handler(nullptr)
{}

template <typename ... Args>
void log(grk_msg_callback msg_handler, void *l_data, char const * const format, Args & ... args) noexcept
{
    const int message_size = 512;
	if ((format != nullptr)) {
		char message[message_size];
		memset(message, 0, message_size);
		vsnprintf(message, message_size, format, args...);
		msg_handler(message, l_data);
	}
}

void GROK_INFO(const char *fmt,	...){
	if (!logger::m_logger.info_handler)
		return;
	va_list arg;
	va_start(arg, fmt);
	log(logger::m_logger.info_handler, logger::m_logger.m_info_data, fmt, arg);
	va_end(arg);
}
void GROK_WARN(const char *fmt,	...){
	if (!logger::m_logger.warning_handler)
		return;
	va_list arg;
	va_start(arg, fmt);
	log(logger::m_logger.warning_handler, logger::m_logger.m_warning_data, fmt, arg);
	va_end(arg);
}
void GROK_ERROR(const char *fmt,...){
	if (!logger::m_logger.error_handler)
		return;
	va_list arg;
	va_start(arg, fmt);
	log(logger::m_logger.error_handler, logger::m_logger.m_error_data, fmt, arg);
	va_end(arg);
}

}
