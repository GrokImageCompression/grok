/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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

namespace grk
{
struct logger
{
	logger();
	void* error_data_;
	void* warning_data_;
	void* info_data_;
	grk_msg_callback error_handler;
	grk_msg_callback warning_handler;
	grk_msg_callback info_handler;

	static logger logger_;
};

void GRK_INFO(const char* fmt, ...);
void GRK_WARN(const char* fmt, ...);
void GRK_ERROR(const char* fmt, ...);

} // namespace grk
