/*
 *    Copyright (C) 2016-2019 Grok Image Compression Inc.
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
 *
 *    This source code incorporates work covered by the following copyright and
 *    permission notice:
 *
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2008, 2011-2012, Centre National d'Etudes Spatiales (CNES), FR
 * Copyright (c) 2012, CS Systemes d'Information, France
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "grok_includes.h"

namespace grk {

const uint32_t EVT_ERROR = 1;	/**< Error event type */
const uint32_t EVT_WARNING= 2;	/**< Warning event type */
const uint32_t EVT_INFO	= 4;	/**< Debug event type */


/**
 * Default callback function.
 * Do nothing.
 */
static void default_callback(const char *msg, void *client_data) {
	ARG_NOT_USED(msg);
	ARG_NOT_USED(client_data);
}

event_mgr_t::event_mgr_t() : m_error_data(nullptr), m_warning_data(nullptr), m_info_data(nullptr),
		        error_handler(default_callback), warning_handler(default_callback), info_handler(default_callback)
{}


template <typename ... Args>
bool log(opj_msg_callback msg_handler, void *l_data, char const * const format, Args & ... args) noexcept
{
    const int message_size = 512;
	if ((format != nullptr)) {
		char message[message_size];
		memset(message, 0, message_size);
		/* parse the format string and put the result in 'message' */
		vsnprintf(message, message_size, format, args...);
		/* output the message to the user program */
		msg_handler(message, l_data);
	}
	return true;
}

bool GROK_INFO(const char *fmt,	...){
	va_list arg;
	va_start(arg, fmt);
	bool rc =
			log(codec_private_t::m_event_mgr.info_handler, codec_private_t::m_event_mgr.m_info_data, fmt, arg);
	va_end(arg);
	return rc;
}
bool GROK_WARN(const char *fmt,	...){
	va_list arg;
	va_start(arg, fmt);
	bool rc =
			log(codec_private_t::m_event_mgr.warning_handler, codec_private_t::m_event_mgr.m_warning_data, fmt, arg);
	va_end(arg);
	return rc;
}
bool GROK_ERROR(const char *fmt,...){
	va_list arg;
	va_start(arg, fmt);
	bool rc =
			log(codec_private_t::m_event_mgr.error_handler, codec_private_t::m_event_mgr.m_error_data, fmt, arg);
	va_end(arg);
	return rc;
}
}
