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

#include "mqc.h"
#include "debug_context.h"

namespace grk
{

// LocationProbe member functions
LocationProbe::LocationProbe(uint8_t passno, uint8_t position, uint16_t i, uint16_t k)
    : passno_(passno), position_(position), i_(i), k_(k)
{}

bool LocationProbe::probe(uint8_t passno, uint8_t position, uint16_t i, uint16_t k) const
{
  return (passno == passno_ && position == position_ && i == i_ && k == k_);
}

DebugContext::DebugContext()
    : compno_(0), resno_(0), precinctIndex_(0), layno_(0), cblkno_(0), numResolutions_(0),
      differentialLayers_(0), debug_backup(false), maxLayers_(0)
{}

// DebugContext singleton instance retrieval
DebugContext& DebugContext::getInstance()
{
  static DebugContext instance;
  return instance;
}

// Handling the mqcoder
bool DebugContext::handle(const t1::mqcoder& mq, uint8_t passno, uint8_t position, uint16_t i,
                          uint16_t k)
{
  uint16_t layer = static_cast<uint16_t>(mq.cur_buffer_index);
  auto& queue = getQueue(layer);
  if(!mq.cached_)
  {
    queue.push(mq);
    logProbe(false, layer, queue.size(), mq.c, passno, position, i, k);
    queueSize_[layer] = queue.size();
    return true;
  }
  else if(!queue.empty())
  {
    if(mq.backup_->i != t1::BACKUP_DISABLED)
      return true;
    t1::mqcoder ref = queue.front();
    queue.pop();
    bool isEqual = (mq == ref);
    logProbe(true, layer, queue.size(), ref.c, passno, position, i, k);
    if(!isEqual)
      printf("Not equal at passno=%d,position=%d,i=%d,k=%d; ref=0x%x,diff=0x%x\n", passno, position,
             i, k, ref.c, mq.c);
    assert(isEqual);
    return isEqual;
  }
  else
  {
    printf("Warning: reference coders are empty\n");
    return true;
  }
}

void DebugContext::restoreBackup(void)
{
  while(!backupQueue_.empty())
  {
    auto& c = backupQueue_.front();
    assert(c.coder_.cached_);
    handle(c.coder_, c.passno_, c.position_, c.i_, c.k_);
    backupQueue_.pop();
  }
}

// Incrementing differential layer
void DebugContext::incrementDifferentialLayer()
{
  if(debug_backup)
  {
    if(++differentialLayers_ == maxLayers_)
    {
      checkEmpty();
      ++cblkno_;
      differentialLayers_ = 0;
    }
  }
}

// Checking if all queues are empty
void DebugContext::checkEmpty() const
{
  for(const auto& [key, queue] : referenceCoders_)
  {
    std::ignore = key;
    assert(queue.empty());
  }
}

// Getting the queue for a specific layer
std::queue<t1::mqcoder>& DebugContext::getQueue(uint16_t layer)
{
  return referenceCoders_[{resno_, layer}];
}

// Logging probe results
void DebugContext::logProbe(bool differential, uint16_t layer, size_t order, uint32_t c,
                            uint8_t passno, uint8_t position, uint16_t i, uint16_t k) const
{
  if(probe_.probe(passno, position, i, k))
  {
    const char* action = differential ? "popped differential" : "pushed full";
    order = differential ? queueSize_[layer] - order : order;
    printf("%s -> layer: %d, position: %d, order: %zu, i=%d,k=%d,c: 0x%x\n", action, layer,
           position, order, i, k, c);
  }
}

} // namespace grk
