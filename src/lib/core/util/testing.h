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
 *
 */

#pragma once

#include <sstream>

namespace grk
{
#if defined(GRK_HAVE_VALGRIND)
#include <valgrind/memcheck.h>
#endif

// uncomment to enable testing of different stages of round-trip lossless compress
// #define DEBUG_LOSSLESS_DWT
// #define DEBUG_LOSSLESS_T1
// #define DEBUG_LOSSLESS_T2

// #define GRK_DEBUG_SPARSE

#ifdef GRK_DEBUG_SPARSE
#undef __SSE__
#undef __SSE2__
#undef __AVX2__
#endif

#if defined(GRK_HAVE_VALGRIND) && defined(GRK_DEBUG_SPARSE)
const size_t grk_mem_ok = (size_t)-1;

#define GRK_DEBUG_VALGRIND
template<typename T>
size_t grk_memcheck(const T* buf, size_t len)
{
   size_t val = VALGRIND_CHECK_MEM_IS_DEFINED(buf, len * sizeof(T));
   return (val) ? (val - (uint64_t)buf) / sizeof(T) : grk_mem_ok;
}
template<typename T>
bool grk_memcheck_all(const T* buf, size_t len, std::string msg)
{
   bool rc = true;
   for(uint32_t i = 0; i < len; ++i)
   {
	  auto val = grk_memcheck<T>(buf + i, 1);
	  if(val != grk_mem_ok)
	  {
		 std::ostringstream ss;
		 ss << msg << " " << "offset = " << i + val;
		 Logger::logger_.error(ss.str().c_str());
		 rc = false;
	  }
   }
   return rc;
}

#endif

} // namespace grk
