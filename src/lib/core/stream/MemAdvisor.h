/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "IMemAdvisor.h"

namespace grk
{

class MemAdvisor
{
private:
  uint8_t* m_ptr; // Base pointer of the mapped buffer
  size_t m_len; // Length of the mapped buffer
  size_t m_initial_offset; // Initial offset applied to virtual offsets

public:
  MemAdvisor(uint8_t* ptr, size_t len, size_t initial_offset)
      : m_ptr(ptr), m_len(len), m_initial_offset(initial_offset)
  {}

  void advise(size_t virtual_offset, size_t length, GrkAccessPattern pattern)
  {
    // Adjust virtual offset to physical offset
    size_t physical_offset = virtual_offset + m_initial_offset;
    if(physical_offset >= m_len)
      return;
    if(length == 0)
      length = m_len - physical_offset;
    if(length == 0)
      return;
    if(physical_offset + length > m_len)
      length = m_len - physical_offset;

#ifndef _WIN32
    long page_size = sysconf(_SC_PAGESIZE);
    if(page_size <= 0)
      throw std::runtime_error("Failed to get page size");

    // Skip madvise for small ranges (< one page)
    if(length < static_cast<size_t>(page_size))
      return;

    uintptr_t base = reinterpret_cast<uintptr_t>(m_ptr);
    uintptr_t start = base + physical_offset;
    uintptr_t end = start + length;

    // Find page-aligned range containing the region
    uintptr_t aligned_start = start & ~(static_cast<uintptr_t>(page_size) - 1);
    uintptr_t aligned_end =
        (end + static_cast<uintptr_t>(page_size) - 1) & ~(static_cast<uintptr_t>(page_size) - 1);

    if(aligned_start >= aligned_end)
      return;

    size_t advise_len = aligned_end - aligned_start;
    void* advise_addr = reinterpret_cast<void*>(aligned_start);

    int advice;
    switch(pattern)
    {
      case GrkAccessPattern::ACCESS_SEQUENTIAL:
        advice = MADV_SEQUENTIAL;
        break;
      case GrkAccessPattern::ACCESS_RANDOM:
        advice = MADV_RANDOM;
        break;
      case GrkAccessPattern::ACCESS_NORMAL:
        advice = MADV_NORMAL;
        break;
      case GrkAccessPattern::ACCESS_DONTNEED:
        advice = MADV_DONTNEED;
        break;
      default:
        throw std::runtime_error("Invalid access pattern");
    }

    if(madvise(advise_addr, advise_len, advice) != 0)
    {
      throw std::runtime_error("madvise failed");
    }
#endif
  }
};

} // namespace grk