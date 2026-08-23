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

// Peak resident memory of this process, for the tests that cap it.
// Linking needs psapi on WIN32.

#pragma once

#include <cstddef>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
// psapi.h must follow windows.h
#include <psapi.h>
#else
#include <sys/resource.h>
#include <sys/time.h>
#endif

static inline size_t peakResidentBytes(void)
{
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters;
  memset(&counters, 0, sizeof(counters));
  if(!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    return 0;
  return (size_t)counters.PeakWorkingSetSize;
#else
  struct rusage usage;
  memset(&usage, 0, sizeof(usage));
  if(getrusage(RUSAGE_SELF, &usage) != 0)
    return 0;
#if defined(__APPLE__)
  // darwin counts ru_maxrss in bytes, linux in kilobytes
  return (size_t)usage.ru_maxrss;
#else
  return (size_t)usage.ru_maxrss * 1024ULL;
#endif
#endif
}
