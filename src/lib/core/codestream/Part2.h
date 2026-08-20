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
#include <vector>

#include "grok.h"

namespace grk
{

// JPEG 2000 Part 2 downsampling factor styles (DFS marker): how one decomposition
// level splits the lower resolution, values as signalled
enum class DecompositionSplit : uint8_t
{
  none = 0,
  both = 1,
  horizontal = 2,
  vertical = 3
};

inline bool splitsHorizontally(DecompositionSplit split)
{
  return split == DecompositionSplit::both || split == DecompositionSplit::horizontal;
}
inline bool splitsVertically(DecompositionSplit split)
{
  return split == DecompositionSplit::both || split == DecompositionSplit::vertical;
}

struct DecompositionStyle
{
  // levels past the signalled ones split both ways
  DecompositionSplit split(uint8_t level) const
  {
    if(level == 0 || level > numLevels)
      return DecompositionSplit::both;
    return levels[level - 1];
  }
  uint8_t numLevels = 0;
  // level 1 is the finest
  DecompositionSplit levels[GRK_MAX_DECOMP_LVLS] = {};
};

// one lifting step of an arbitrary transformation kernel (ATK marker), stored in the
// synthesis order the marker signals them
struct LiftingStep
{
  // position of the first coefficient relative to the updated sample
  int8_t offset = 0;
  // reversible kernels divide the weighted sum by 2^downshift after adding rounding
  uint8_t downshift = 0;
  int16_t rounding = 0;
  std::vector<double> coefficients;
};

struct TransformKernel
{
  bool reversible = false;
  // whole sample symmetric steps, signalled with half their coefficients
  bool symmetric = false;
  bool symmetricExtension = false;
  // parity of the last analysis step: 0 updates odd samples, 1 even samples
  uint8_t lastStepParity = 0;
  double scale = 1.0;
  std::vector<LiftingStep> steps;
};

} // namespace grk
