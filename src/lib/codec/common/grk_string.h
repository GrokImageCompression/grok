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
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.

 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

#include <errno.h>
#include <string.h>

namespace grk
{
/* strnlen is not standard, strlen_s is C11... */
/* keep in mind there still is a buffer read overflow possible */
static size_t strnlen_s(const char* src, size_t max_len)
{
	size_t len;

	if(src == nullptr)
	{
		return 0U;
	}
	for(len = 0U; (*src != '\0') && (len < max_len); src++, len++)
		;
	return len;
}

/* should be equivalent to C11 function except for the handler */
/* keep in mind there still is a buffer read overflow possible */
static int strcpy_s(char* dst, size_t dst_size, const char* src)
{
	size_t src_len = 0U;
	if((dst == nullptr) || (dst_size == 0U))
	{
		return EINVAL;
	}
	if(src == nullptr)
	{
		dst[0] = '\0';
		return EINVAL;
	}
	src_len = strnlen_s(src, dst_size);
	if(src_len >= dst_size)
	{
		return ERANGE;
	}
	memcpy(dst, src, src_len);
	dst[src_len] = '\0';
	return 0;
}

} // namespace grk
