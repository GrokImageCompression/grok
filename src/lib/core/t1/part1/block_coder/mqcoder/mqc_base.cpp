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

#include "mqc_base.h"

namespace grk
{

/**
 * @brief Creates an mqcoder_base
 */
mqcoder_base::mqcoder_base(bool cached)
    : c(0), a(0), ct(0), end_of_byte_stream_counter(0), bp(nullptr), curctx(nullptr),
      curctx_index_(0), cached_(cached), finalLayer_(false)
{}

// Copy constructor
mqcoder_base::mqcoder_base(const mqcoder_base& other)
{
  *this = other;
}

void mqcoder_base::reinit(void)
{
  c = 0;
  a = 0;
  ct = 0;
  end_of_byte_stream_counter = 0;
  bp = nullptr;
  curctx = nullptr;
  curctx_index_ = 0;
  finalLayer_ = false;
}

// Assignment operator for mqcoder_base
mqcoder_base& mqcoder_base::operator=(const mqcoder_base& other)
{
  if(this != &other)
  {
    c = other.c;
    a = other.a;
    ct = other.ct;
    end_of_byte_stream_counter = other.end_of_byte_stream_counter;
    bp = nullptr;
    cached_ = other.cached_;
    finalLayer_ = other.finalLayer_;

    // Copy ctxs array
    for(int i = 0; i < MQC_NUMCTXS; i++)
    {
      ctxs[i] = other.ctxs[i];
    }
    curctx_index_ = other.curctx - &other.ctxs[0];
    curctx = &ctxs[curctx_index_];
  }
  return *this;
}

bool mqcoder_base::operator==(const mqcoder_base& other) const
{
  bool rc = true;

  if(c != other.c || a != other.a || ct != other.ct)
  {
    rc = false;
  }

  if(curctx - &ctxs[0] != other.curctx - &other.ctxs[0])
    rc = false;

  if(*curctx != *other.curctx)
    rc = false;

  // Compare ctxs array element by element
  for(int i = 0; i < MQC_NUMCTXS; i++)
  {
    if(ctxs[i] != other.ctxs[i])
    {
      rc = false;
    }
  }

  return rc;
}

void mqcoder_base::print(const std::string& msg)
{
  printf("\n%s c: 0x%x, a: 0x%x, ct: 0x%x, end_count: %d, bp: %p\n", msg.c_str(), c, a, ct,
         end_of_byte_stream_counter, bp);
}

} // namespace grk
