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

#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"


namespace grk
{

void grk_write(uint8_t* dest, const uint8_t* value, [[maybe_unused]] uint8_t sizeOfType,
               uint32_t numBytes)
{
  if(numBytes == 0)
    return;
  assert(numBytes <= sizeOfType);
#if defined(GROK_BIG_ENDIAN)
  const uint8_t* src = value + sizeOfType - numBytes;
  memcpy(dest, src, numBytes);
#else
  const uint8_t* src = value + (size_t)(numBytes - 1);
  for(uint32_t i = 0; i < numBytes; ++i)
    *(dest++) = *(src--);
#endif
}

} // namespace grk