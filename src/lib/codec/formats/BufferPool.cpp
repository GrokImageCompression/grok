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

#include <BufferPool.h>

BufferPool::BufferPool() {}

BufferPool::~BufferPool()
{
  for(auto& p : pool)
    p.second.dealloc();
}

GrkIOBuf BufferPool::get(size_t len)
{
  for(auto iter = pool.begin(); iter != pool.end(); ++iter)
  {
    if(iter->second.alloc_len >= len)
    {
      auto b = iter->second;
      b.len = len;
      pool.erase(iter);
      // printf("Buffer pool get  %p\n", b.data);
      return b;
    }
  }
  GrkIOBuf rc;
  rc.alloc(len);

  return rc;
}
void BufferPool::put(GrkIOBuf b)
{
  assert(b.data);
  assert(!pool.contains(b.data));
  pool[b.data] = b;
}
