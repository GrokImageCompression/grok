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

#include <queue>
#include <map>
#include <cassert>
#include <cstdint>

namespace grk
{

using ResolutionLayerKey = std::pair<uint8_t, uint16_t>;
using QueueMap = std::map<ResolutionLayerKey, std::queue<t1::mqcoder>>;

struct BackupCache
{
  BackupCache(const t1::mqcoder& coder, uint8_t passno, uint8_t position, uint16_t i, uint16_t k)
      : coder_(coder), passno_(passno), position_(position), i_(i), k_(k)
  {}
  t1::mqcoder coder_;
  uint8_t passno_;
  uint8_t position_;
  uint16_t i_;
  uint16_t k_;
};

struct LocationProbe
{
  uint8_t passno_ = 0xFF;
  uint8_t position_ = 0xFF;
  uint16_t i_ = 0xFFFF;
  uint16_t k_ = 0xFFFF;

  LocationProbe() = default;
  LocationProbe(uint8_t passno, uint8_t position, uint16_t i, uint16_t k);

  bool probe(uint8_t passno, uint8_t position, uint16_t i, uint16_t k) const;
};

class DebugContext
{
public:
  static DebugContext& getInstance();

  bool handle(const t1::mqcoder& mq, uint8_t passno, uint8_t position, uint16_t i, uint16_t k);
  void incrementDifferentialLayer();
  void restoreBackup(void);

  uint16_t compno_;
  uint8_t resno_;
  uint64_t precinctIndex_;
  uint16_t layno_;
  uint32_t cblkno_;
  uint8_t numResolutions_;
  uint16_t differentialLayers_;
  bool debug_backup;
  uint16_t maxLayers_;
  LocationProbe probe_;

private:
  DebugContext();
  void checkEmpty() const;
  std::queue<t1::mqcoder>& getQueue(uint16_t layer);
  void logProbe(bool differential, uint16_t layer, size_t order, uint32_t c, uint8_t passno,
                uint8_t position, uint16_t i, uint16_t k) const;

  QueueMap referenceCoders_;
  std::queue<BackupCache> backupQueue_;
  size_t queueSize_[256] = {0}; // Initialize all elements to 0
};

} // namespace grk
