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
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

namespace grk {

struct logger {

	logger();
	void *m_error_data;
	void *m_warning_data;
	void *m_info_data;
	grk_msg_callback error_handler;
	grk_msg_callback warning_handler;
	grk_msg_callback info_handler;

	static logger m_logger;
};


void GROK_INFO(const char *fmt,	...);
void GROK_WARN(const char *fmt,	...);
void GROK_ERROR(const char *fmt,...);



}
