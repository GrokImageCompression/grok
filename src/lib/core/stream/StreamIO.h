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

#include <bit>

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

// stream data is big endian
template<typename TYPE>
void grk_read(const uint8_t* src, TYPE* value, uint32_t numBytes)
{
  assert(numBytes > 0 && numBytes <= sizeof(TYPE));
  if(numBytes == 0 || numBytes > sizeof(TYPE))
    throw std::runtime_error("read size too large");

  *value = 0;
  if constexpr(std::endian::native == std::endian::big)
  {
    memcpy((uint8_t*)value + sizeof(TYPE) - numBytes, src, numBytes);
  }
  else
  {
    auto dest = (uint8_t*)value;
    for(uint32_t i = 0; i < numBytes; ++i)
      dest[i] = src[numBytes - 1 - i];
  }
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