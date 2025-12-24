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

#pragma once

#include <errno.h>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace grk
{

// Simplified and safe version of strnlen_s
static size_t strnlen_s(const char* src, size_t max_len)
{
  if(src == nullptr)
  {
    return 0U;
  }
  return std::char_traits<char>::length(src) < max_len ? std::char_traits<char>::length(src)
                                                       : max_len;
}

// Simplified and safe version of strcpy_s
static int strcpy_s(char* dst, size_t dst_size, const char* src)
{
  if(dst == nullptr || dst_size == 0U)
  {
    return EINVAL;
  }
  if(src == nullptr)
  {
    dst[0] = '\0';
    return EINVAL;
  }
  size_t src_len = strnlen_s(src, dst_size);
  if(src_len >= dst_size)
  {
    dst[0] = '\0'; // Ensure null-termination on error
    return ERANGE;
  }
  std::strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = '\0'; // Ensure null-termination
  return 0;
}

} // namespace grk
