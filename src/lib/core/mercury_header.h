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
 */

#pragma once

#include <cstdint>
#include <vector>

#include "mercury_capi.h"

namespace grk
{
struct CodingParams;
struct TileCodingParams;
class GrkImage;

struct MercuryCodingHeader
{
  MercuryCodingHeader() = default;
  // header points into the vectors
  MercuryCodingHeader(const MercuryCodingHeader&) = delete;
  MercuryCodingHeader& operator=(const MercuryCodingHeader&) = delete;

  std::vector<MercurySizComp> comps;
  std::vector<MercuryPrecinct> precincts;
  std::vector<uint8_t> qcdRanges;
  std::vector<float> qcdSteps;
  std::vector<MercuryQccOverride> qcc;
  std::vector<std::vector<uint8_t>> qccRanges;
  std::vector<std::vector<float>> qccSteps;
  std::vector<MercuryCocOverride> coc;
  std::vector<std::vector<MercuryPrecinct>> cocPrecincts;
  std::vector<MercuryProgressionVolume> poc;
  MercuryMainHeader header;
};

void fillMercuryCodingHeader(MercuryCodingHeader& coding, const CodingParams& cp,
                             TileCodingParams& tcp, const GrkImage& components,
                             const GrkImage& canvas);

} // namespace grk
