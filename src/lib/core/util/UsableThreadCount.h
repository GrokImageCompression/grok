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

#include <bitset>
#include <cstdint>
#include <thread>

#if defined(__linux__)
#include <sched.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace grk
{

inline uint32_t usableThreadCount(void)
{
#if defined(__linux__)
  cpu_set_t affinityMask;
  CPU_ZERO(&affinityMask);
  if(sched_getaffinity(0, sizeof(affinityMask), &affinityMask) == 0)
  {
    auto affinityCount = CPU_COUNT(&affinityMask);
    if(affinityCount > 0)
      return static_cast<uint32_t>(affinityCount);
  }
#elif defined(_WIN32)
  DWORD_PTR processMask = 0;
  DWORD_PTR systemMask = 0;
  if(GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask))
  {
    auto affinityCount = std::bitset<sizeof(DWORD_PTR) * 8>(processMask).count();
    if(affinityCount > 0)
      return static_cast<uint32_t>(affinityCount);
  }
#endif
  auto hardwareCount = std::thread::hardware_concurrency();
  return hardwareCount ? hardwareCount : 1;
}

} // namespace grk
