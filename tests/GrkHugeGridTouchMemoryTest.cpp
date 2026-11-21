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

#include <cstdio>
#include <memory_resource>

#include "BitIO.h"
#include "TagTree.h"
#include "BlockCache.h"

namespace
{
class CountingResource : public std::pmr::memory_resource
{
public:
  size_t allocatedBytes = 0;

private:
  void* do_allocate(size_t bytes, size_t alignment) override
  {
    allocatedBytes += bytes;
    return std::pmr::new_delete_resource()->allocate(bytes, alignment);
  }
  void do_deallocate(void* p, size_t bytes, size_t alignment) override
  {
    std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
  {
    return this == &other;
  }
};

struct CodeBlockStandIn
{
  explicit CodeBlockStandIn(uint16_t) {}
  bool empty() const
  {
    return !initialized;
  }
  bool initialized = false;
  char payload[360] = {};
};

struct CodeBlockInitializer
{
  void initCodeBlock(CodeBlockStandIn* block, uint32_t)
  {
    block->initialized = true;
  }
};

const size_t TOUCH_BYTES_LIMIT = 16 * 1024;
bool checkTouchBytes(const char* name, size_t bytes)
{
  if(bytes <= TOUCH_BYTES_LIMIT)
    return true;
  fprintf(stderr, "%s allocated %zu bytes, limit %zu\n", name, bytes, TOUCH_BYTES_LIMIT);
  return false;
}
} // namespace

const uint16_t GRID_SIZE = 8192;
const uint64_t TOUCHED_INDEX = (uint64_t)(GRID_SIZE / 2) * GRID_SIZE + GRID_SIZE / 2;
int main()
{
  bool passed = true;
  {
    CountingResource arena;
    grk::TagTreeU16 tree(GRID_SIZE, GRID_SIZE, &arena);
    tree.set(TOUCHED_INDEX, 0);
    passed &= checkTouchBytes("tag tree", arena.allocatedBytes);
  }
  {
    CountingResource arena;
    CodeBlockInitializer initializer;
    grk::BlockCache<CodeBlockStandIn, CodeBlockInitializer> cache(1, GRID_SIZE, GRID_SIZE,
                                                                  &initializer, &arena);
    auto block = cache.get(TOUCHED_INDEX);
    if(cache.tryGet(TOUCHED_INDEX) != block || cache.tryGet(TOUCHED_INDEX + 1))
    {
      fprintf(stderr, "block cache returned the wrong block\n");
      passed = false;
    }
    passed &= checkTouchBytes("block cache", arena.allocatedBytes);
  }
  return passed ? 0 : 1;
}
