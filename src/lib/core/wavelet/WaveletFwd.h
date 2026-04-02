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

#include <cstdint>
#include <memory>
#include <vector>
#include <utility>
#include "TileComponent.h"
#include "WaveletReverse.h"

class FlowComponent;

namespace grk
{

// Opaque handle owning scratch buffers for scheduled DWT.
// Must be kept alive until after DAG execution completes.
struct WaveletFwdScheduleData
{
  virtual ~WaveletFwdScheduleData() = default;
};

class WaveletFwdImpl
{
public:
  virtual ~WaveletFwdImpl() = default;
  bool compress(TileComponent* tile_comp, uint8_t qmfbid, uint32_t maxDim,
                DcShiftParam dcShift = {}, bool intInput = false);
  // Schedule forward DWT into FlowComponent pairs (vert, horiz) per level.
  // Returns an opaque handle owning scratch buffers; caller must keep it alive
  // until DAG execution completes.
  // intInput: when true, first-level 9/7 reads int32_t from tile buffer and converts to float.
  //           Set to true for non-MCT irreversible components (MCT already converts to float).
  std::unique_ptr<WaveletFwdScheduleData> scheduleCompress(
      TileComponent* tile_comp, uint8_t qmfbid, uint32_t maxDim, DcShiftParam dcShift,
      std::vector<std::pair<FlowComponent*, FlowComponent*>>& levelFlows, bool intInput = false);
};

} // namespace grk
