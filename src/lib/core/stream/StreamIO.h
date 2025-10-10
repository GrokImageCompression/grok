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

namespace grk
{

void grk_write(uint8_t* dest, const uint8_t* value, [[maybe_unused]] uint8_t sizeOfType,
               uint32_t numBytes);

template<typename TYPE>
void grk_write(uint8_t* dest, TYPE value, uint32_t numBytes)
{
  grk_write(dest, (const uint8_t*)&value, sizeof(TYPE), numBytes);
}

template<typename TYPE>
void grk_write(uint8_t* dest, TYPE value)
{
  grk_write(dest, value, sizeof(TYPE));
}

template<typename TYPE>
void grk_write(uint8_t** dest, TYPE value)
{
  grk_write(*dest, value, sizeof(TYPE));
  *dest += sizeof(TYPE);
}

template<typename TYPE>
void grk_read(const uint8_t* src, TYPE* value, uint32_t numBytes)
{
  assert(numBytes > 0 && numBytes <= sizeof(TYPE));
  if(numBytes == 0 || numBytes > sizeof(TYPE))
    throw std::runtime_error("read size too large");

  *value = 0;
  memcpy(value, src, numBytes); // Copy bytes directly
#if defined(_MSC_VER) // MSVC
  if(numBytes == 8)
  {
    *value = (TYPE)_byteswap_uint64((uint64_t)*value); // Big-endian to little-endian
  }
  else if(numBytes == 4)
  {
    *value = (TYPE)_byteswap_ulong((uint32_t)*value);
  }
  else if(numBytes == 2)
  {
    *value = (TYPE)_byteswap_ushort((uint16_t)*value);
  }
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__) // POSIX with byteswap.h
#include <byteswap.h>
  if(numBytes == 8)
  {
    *value = (TYPE)bswap_64((uint64_t)*value);
  }
  else if(numBytes == 4)
  {
    *value = (TYPE)bswap_32((uint32_t)*value);
  }
  else if(numBytes == 2)
  {
    *value = (TYPE)bswap_16((uint16_t)*value);
  }
#else // Fallback for other POSIX systems
  if(numBytes == 8)
  {
    uint64_t tmp = *(uint64_t*)value;
    *value = (TYPE)(((tmp >> 56) & 0xFF) | ((tmp >> 40) & 0xFF00) | ((tmp >> 24) & 0xFF0000) |
                    ((tmp >> 8) & 0xFF000000) | ((tmp << 8) & 0xFF00000000) |
                    ((tmp << 24) & 0xFF0000000000) | ((tmp << 40) & 0xFF000000000000) |
                    ((tmp << 56) & 0xFF00000000000000));
  }
  else if(numBytes == 4)
  {
    uint32_t tmp = *(uint32_t*)value;
    *value = (TYPE)(((tmp >> 24) & 0xFF) | ((tmp >> 8) & 0xFF00) | ((tmp << 8) & 0xFF0000) |
                    ((tmp << 24) & 0xFF000000));
  }
  else if(numBytes == 2)
  {
    uint16_t tmp = *(uint16_t*)value;
    *value = (TYPE)(((tmp >> 8) & 0xFF) | ((tmp << 8) & 0xFF00));
  }
#endif
}

template<typename TYPE>
void grk_read(uint8_t** src, uint32_t* bytesRemaining, TYPE* value, uint32_t numBytes)
{
  grk_read(*src, value, numBytes);
  *src += numBytes;
  if(bytesRemaining)
  {
    if(*bytesRemaining < numBytes)
      throw std::runtime_error("grk_read: not enough bytes to read data");
    *bytesRemaining -= numBytes;
  }
}

template<typename TYPE>
void grk_read(uint8_t** src, TYPE* value, uint32_t numBytes)
{
  grk_read(src, nullptr, value, numBytes);
}

template<typename TYPE>
void grk_read(const uint8_t* dest, TYPE* value)
{
  grk_read<TYPE>(dest, value, sizeof(TYPE));
}

template<typename TYPE>
void grk_read(uint8_t** dest, uint32_t* bytesRemaining, TYPE* value)
{
  grk_read<TYPE>(*dest, value, sizeof(TYPE));
  *dest += sizeof(TYPE);
  if(bytesRemaining)
  {
    if(*bytesRemaining < sizeof(TYPE))
      throw std::runtime_error("grk_read: not enough bytes to read data");
    *bytesRemaining -= (uint32_t)sizeof(TYPE);
  }
}

template<typename TYPE>
void grk_read(uint8_t** dest, TYPE* value)
{
  return grk_read(dest, nullptr, value);
}

} // namespace grk