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
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "IStreamWriter.h"

namespace grk::t1
{

class TruncatedPacketHeaderException : public std::exception
{
};

class InvalidMarkerException : public std::exception
{
public:
  class BadAsocException : public std::exception
  {
  };

  class CorruptMarkerException : public std::exception
  {
  };
  explicit InvalidMarkerException(uint16_t marker) : marker_(marker) {}

  uint16_t marker_;
};

/**
 * @class t1::BitIO
 * @brief Bit IO
 */
class BitIO
{
public:
  /**
   * @brief Constructs a t1::BitIO object
   * @param bp bit buffer
   * @param len length of bit buffer
   * @param isCompressor flag indicating compression or lack thereof
   */
  BitIO(uint8_t* bp, uint64_t len, bool isCompressor)
      : start(bp), offset(0), buf_len(len), buf(0), ct(isCompressor ? 8 : 0), stream(nullptr),
        read0xFF(false)
  {
    assert(isCompressor || bp);
  }

  /**
   * @brief Constructs a t1::BitIO object
   * @param stream IStreamWriter
   * @param isCompressor flag indicating compression or lack thereof
   */
  BitIO(IStreamWriter* stream, bool isCompressor)
      : start(nullptr), offset(0), buf_len(0), buf(0), ct(isCompressor ? 8 : 0), stream(stream),
        read0xFF(false)
  {}

  /**
   * @brief Gets number of bytes written
   * @return number of bytes written
   */
  size_t numBytes()
  {
    return offset;
  }

  /**
   * @brief Writes bits
   * @param v bits to write
   * @param n number of bits to write (must be <= 32)
   * @return true if successful
   */
  bool write(uint32_t v, uint8_t n)
  {
    assert(n != 0 && n <= 32);
    for(int8_t i = (int8_t)(n - 1); i >= 0; i--)
    {
      if(!putbit((v >> i) & 1))
        return false;
    }
    return true;
  }

  /**
   * @brief Writes one bit
   * @param v bit to write
   * @return true if successful
   */
  bool write(uint8_t v)
  {
    return putbit(v & 1);
  }

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
  uint8_t read(void)
  {
    return getbit();
  }

  /**
   * @brief Flushes remaining bits
   * @return true if successful
   */
  bool flush()
  {
    if(!write8u())
      return false;

    return (ct == 7) ? write8u() : true;
  }

  /**
   * @brief Reads bits at end of packet header
   */
  void readFinalHeaderByte()
  {
    if(buf == 0xff)
      bytein();
    ct = 0;
  }

  /**
   * @brief Writes comma code
   * @param n comma code
   * @return true if successful
   */
  bool putcommacode(uint8_t n)
  {
    int16_t nn = n;
    while(--nn >= 0)
    {
      if(!write(1))
        return false;
    }

    return write(0);
  }

  /**
   * @brief Reads comma code
   * @return comma code
   */
  uint8_t getcommacode(void)
  {
    uint8_t n = 0;
    uint8_t temp = read();
    while(temp)
    {
      n++;
      temp = read();
    }

    return n;
  }

  /**
   * @brief Writes number of passes
   * @param n number of passes
   */
  bool putnumpasses(uint8_t n)
  {
    if(n == 1)
    {
      if(!write(0))
        return false;
    }
    else if(n == 2)
    {
      if(!write(2, 2))
        return false;
    }
    else if(n <= 5)
    {
      if(!write(0xc | (n - 3), 4))
        return false;
    }
    else if(n <= 36)
    {
      if(!write(0x1e0 | (n - 6), 9))
        return false;
    }
    else if(n <= 164)
    {
      if(!write(0xff80 | (n - 37), 16))
        return false;
    }

    return true;
  }

  /**
   * @brief Reads number of passes
   * @param numpasses pointer to receive number of passes
   */
  void getnumpasses(uint8_t* numpasses)
  {
    uint32_t n = read();
    if(!n)
    {
      *numpasses = 1;
      return;
    }
    n = read();
    if(!n)
    {
      *numpasses = 2;
      return;
    }
    read(&n, 2);
    if(n != 3)
    {
      *numpasses = (uint8_t)(n + 3);
      return;
    }
    read(&n, 5);
    if(n != 31)
    {
      *numpasses = (uint8_t)(n + 6);
      return;
    }
    read(&n, 7);
    *numpasses = (uint8_t)(n + 37);
  }

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

  IStreamWriter* stream;
  bool read0xFF;

  bool putbit(uint8_t b)
  {
    if(ct == 0 && !write8u())
      return false;
    ct--;
    buf = static_cast<uint8_t>(buf | (b << ct));

    return true;
  }

  uint8_t getbit(void)
  {
    if(ct == 0)
      bytein();
    assert(ct > 0);
    ct = (uint8_t)(ct - 1);

    return (buf >> ct) & 1;
  }

  bool write8u(void)
  {
    if(stream)
    {
      if(!stream->write8u(buf))
        return false;
    }
    else
    {
      // avoid buffer over-run
      if(offset == buf_len)
        return false;

      offset++;
    }
    ct = buf == 0xff ? 7 : 8;
    buf = 0;

    return true;
  }

  void bytein(void)
  {
    if(offset == buf_len)
      throw TruncatedPacketHeaderException();
    if(read0xFF && (buf >= 0x90))
    {
      uint16_t marker = (uint16_t)(((uint16_t)0xFF << 8) | (uint16_t)buf);
      // if(marker != EPH && marker != SOP)
      //   grklog.warn("Invalid marker 0x%x detected in packet header", marker);
      // else
      //   grklog.warn("Unexpected SOP/EPH marker 0x%x detected in packet header", marker);

      throw InvalidMarkerException(marker);
    }
    read0xFF = (buf == 0xff);
    ct = read0xFF ? 7 : 8;
    buf = start[offset];
    offset++;
  }
};

} // namespace grk::t1