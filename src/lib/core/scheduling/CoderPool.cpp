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

#include "grk_includes.h"

namespace grk
{

bool CoderPool::contains(uint8_t maxCblkW, uint8_t maxCblkH)
{
  auto key = CoderKey(maxCblkW, maxCblkH);
  return coderMap_.find(key) != coderMap_.end();
}

void CoderPool::makeCoders(uint32_t numCoders, uint8_t maxCblkW, uint8_t maxCblkH,
                           std::function<std::shared_ptr<ICoder>()> creator)
{
  if(contains(maxCblkW, maxCblkH))
    return;
  std::vector<std::shared_ptr<ICoder>> coders;
  for(uint32_t i = 0; i < numCoders; ++i)
    coders.push_back(creator());
  coderMap_[{maxCblkW, maxCblkH}] = std::move(coders);
}
std::shared_ptr<ICoder> CoderPool::getCoder(size_t worker, uint8_t maxCblkW, uint8_t maxCblkH)
{
  auto it = coderMap_.find({maxCblkW, maxCblkH});
  if(it == coderMap_.end())
  {
    throw std::out_of_range("CoderKey not found in coderMap");
  }
  if(worker >= it->second.size())
  {
    throw std::out_of_range("Worker index out of bounds");
  }
  return it->second[worker];
}

} // namespace grk