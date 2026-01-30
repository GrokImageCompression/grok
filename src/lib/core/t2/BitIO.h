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

#include <cassert>
#include "IStream.h"

namespace grk
{

/**
 * @class BitIO
 * @brief Bit IO
 */
class BitIO
{
public:
  /**
   * @brief Constructs a BitIO object
   * @param bp bit buffer
   * @aram len length of bit buffer
   * @param isCompressor flag indicating compression or lack thereof
   */
  BitIO(uint8_t* bp, uint64_t len, bool isCompressor);

  /**
   * @brief Constructs a BitIO objecxt
   * @param @ref IStream
   * @param isCompressor flag indicating compression or lack thereof
   */
  BitIO(IStream* stream, bool isCompressor);

  /**
   * @brief Gets number of bytes written
   * @return number of bytes written
   */
  size_t numBytes();

  /**
   * @brief Writes bits
   * @param v bits to write
   * @param n number of bits to write (must be <= 32)
   * @return true if successful
   */
  bool write(uint32_t v, uint8_t n);

  /**
   * @brief Writes one bit
   * @param v bit to write
   * @return true if successful
   */
  bool write(uint8_t v);

  /**
   * @brief Reads bits
   * @param bits pointer to bits buffer
   * @param n number of bits to read (must be <= sizeof(T) << 3)
   */
  template<typename T>
  void read(T* bits, uint8_t n)
  {
    assert(n > 0 && n <= sizeof(T) << 3);
    *bits = 0U;
    for(int8_t i = (int8_t)(n - 1); i >= 0; i--)
    {
      if(ct == 0)
        bytein();
      assert(ct > 0);
      ct = (uint8_t)(ct - 1);
      *bits |= (T)(((buf >> ct) & 1) << i);
    }
  }

  /**
   * @brief Reads bit
   * @return bit that was read
   */
  uint8_t read(void);

  /**
   * @brief Flushes remaining bits
   * @return true if successful
   */
  bool flush();

  /**
   * @brief Reads bits at end of packet header
   */
  void readFinalHeaderByte();
  /**
   * @brief Writes comma code
   * @param n comma code
   * @return true if successful
   */
  bool putcommacode(uint8_t n);

  /**
   * @brief Reads comma code
   * @return comma code
   */
  uint8_t getcommacode(void);

  /**
   * @brief Writes number of passes
   * @param n number of passes
   */
  bool putnumpasses(uint8_t n);

  /**
   * @brief Reads number of passes
   * @param numpasses pointer to receive number of passes
   */
  void getnumpasses(uint8_t* numpasses);

private:
  /* pointer to the start of the buffer */
  uint8_t* start;
  size_t offset;
  size_t buf_len;

  /**
   * @brief Temporary buffer where bytes are read from or written to
   */
  uint8_t buf;

  /**
   * @brief Number of bits free to write for encoder or number of bits
   * to read for decoder
   */
  uint8_t ct;

  IStream* stream;
  bool read0xFF;

  bool putbit(uint8_t b);
  uint8_t getbit(void);
  bool write8u(void);
  void bytein(void);
};

} // namespace grk
