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

#include "SelectiveFetchRanges.h"

#include <algorithm>
#include <cassert>

namespace grk
{

uint64_t computeTotalPackets(const TilePacketInfo& info)
{
  uint64_t total = 0;
  for(uint16_t c = 0; c < info.numComponents; ++c)
  {
    for(uint8_t r = 0; r < info.numResolutions[c]; ++r)
    {
      total += (uint64_t)info.numLayers * info.precinctsPerRes[c][r];
    }
  }
  return total;
}

// Internal: walk packets in progression order, yielding (compno, resno, precinctIndex, layno)
// and whether the packet is needed for the target resolution.
// Returns a vector of bools parallel to PLT lengths: true = needed, false = not needed.
static std::vector<bool> computePacketMask(const TilePacketInfo& info,
                                           uint8_t resolutionsToDecompress)
{
  uint64_t totalPackets = computeTotalPackets(info);
  std::vector<bool> mask(totalPackets, false);

  // We need to iterate in the same order as PacketIter would.
  // For simplicity, assume single progression (no POC) and no subsampling
  // (all components have same number of resolutions).

  uint8_t maxRes = 0;
  for(uint16_t c = 0; c < info.numComponents; ++c)
    maxRes = std::max(maxRes, info.numResolutions[c]);

  uint64_t packetIndex = 0;

  switch(info.progression)
  {
    case GRK_LRCP:
      // Layer → Resolution → Component → Precinct
      for(uint16_t l = 0; l < info.numLayers; ++l)
      {
        for(uint8_t r = 0; r < maxRes; ++r)
        {
          for(uint16_t c = 0; c < info.numComponents; ++c)
          {
            if(r >= info.numResolutions[c])
              continue;
            uint64_t numPrecincts = info.precinctsPerRes[c][r];
            for(uint64_t p = 0; p < numPrecincts; ++p)
            {
              assert(packetIndex < totalPackets);
              mask[packetIndex] = (r < resolutionsToDecompress);
              packetIndex++;
            }
          }
        }
      }
      break;

    case GRK_RLCP:
      // Resolution → Layer → Component → Precinct
      for(uint8_t r = 0; r < maxRes; ++r)
      {
        for(uint16_t l = 0; l < info.numLayers; ++l)
        {
          for(uint16_t c = 0; c < info.numComponents; ++c)
          {
            if(r >= info.numResolutions[c])
              continue;
            uint64_t numPrecincts = info.precinctsPerRes[c][r];
            for(uint64_t p = 0; p < numPrecincts; ++p)
            {
              assert(packetIndex < totalPackets);
              mask[packetIndex] = (r < resolutionsToDecompress);
              packetIndex++;
            }
          }
        }
      }
      break;

    case GRK_RPCL:
      // Resolution → Precinct → Component → Layer
      for(uint8_t r = 0; r < maxRes; ++r)
      {
        // For RPCL, we need the max precincts across components at this resolution
        uint64_t maxPrecincts = 0;
        for(uint16_t c = 0; c < info.numComponents; ++c)
        {
          if(r < info.numResolutions[c])
            maxPrecincts = std::max(maxPrecincts, info.precinctsPerRes[c][r]);
        }
        for(uint64_t p = 0; p < maxPrecincts; ++p)
        {
          for(uint16_t c = 0; c < info.numComponents; ++c)
          {
            if(r >= info.numResolutions[c])
              continue;
            if(p >= info.precinctsPerRes[c][r])
              continue;
            for(uint16_t l = 0; l < info.numLayers; ++l)
            {
              assert(packetIndex < totalPackets);
              mask[packetIndex] = (r < resolutionsToDecompress);
              packetIndex++;
            }
          }
        }
      }
      break;

    case GRK_PCRL:
      // Precinct → Component → Resolution → Layer
      // Need max precincts across all resolutions and components
      {
        uint64_t globalMaxPrecincts = 0;
        for(uint16_t c = 0; c < info.numComponents; ++c)
          for(uint8_t r = 0; r < info.numResolutions[c]; ++r)
            globalMaxPrecincts = std::max(globalMaxPrecincts, info.precinctsPerRes[c][r]);

        for(uint64_t p = 0; p < globalMaxPrecincts; ++p)
        {
          for(uint16_t c = 0; c < info.numComponents; ++c)
          {
            for(uint8_t r = 0; r < info.numResolutions[c]; ++r)
            {
              if(p >= info.precinctsPerRes[c][r])
                continue;
              for(uint16_t l = 0; l < info.numLayers; ++l)
              {
                assert(packetIndex < totalPackets);
                mask[packetIndex] = (r < resolutionsToDecompress);
                packetIndex++;
              }
            }
          }
        }
      }
      break;

    case GRK_CPRL:
      // Component → Precinct → Resolution → Layer
      for(uint16_t c = 0; c < info.numComponents; ++c)
      {
        uint64_t compMaxPrecincts = 0;
        for(uint8_t r = 0; r < info.numResolutions[c]; ++r)
          compMaxPrecincts = std::max(compMaxPrecincts, info.precinctsPerRes[c][r]);

        for(uint64_t p = 0; p < compMaxPrecincts; ++p)
        {
          for(uint8_t r = 0; r < info.numResolutions[c]; ++r)
          {
            if(p >= info.precinctsPerRes[c][r])
              continue;
            for(uint16_t l = 0; l < info.numLayers; ++l)
            {
              assert(packetIndex < totalPackets);
              mask[packetIndex] = (r < resolutionsToDecompress);
              packetIndex++;
            }
          }
        }
      }
      break;

    default:
      return {};
  }

  assert(packetIndex == totalPackets);
  return mask;
}

std::vector<FetchRange> computeSelectiveFetchRanges(const TilePacketInfo& info,
                                                    uint8_t resolutionsToDecompress,
                                                    uint64_t gapThreshold)
{
  if(info.pltLengths.empty() || info.numComponents == 0)
    return {};

  // Validate
  uint64_t totalPackets = computeTotalPackets(info);
  if(info.pltLengths.size() != totalPackets)
    return {};

  // If requesting all resolutions, return single range covering everything
  uint8_t maxRes = 0;
  for(uint16_t c = 0; c < info.numComponents; ++c)
    maxRes = std::max(maxRes, info.numResolutions[c]);
  if(resolutionsToDecompress >= maxRes)
  {
    uint64_t totalBytes = 0;
    for(auto len : info.pltLengths)
      totalBytes += len;
    return {{0, totalBytes}};
  }

  auto mask = computePacketMask(info, resolutionsToDecompress);
  if(mask.empty())
    return {};

  // Build ranges from mask + PLT lengths
  std::vector<FetchRange> ranges;
  uint64_t offset = 0;
  bool inRange = false;
  uint64_t rangeStart = 0;

  for(uint64_t i = 0; i < totalPackets; ++i)
  {
    if(mask[i])
    {
      if(!inRange)
      {
        rangeStart = offset;
        inRange = true;
      }
    }
    else
    {
      if(inRange)
      {
        ranges.push_back({rangeStart, offset - rangeStart});
        inRange = false;
      }
    }
    offset += info.pltLengths[i];
  }
  if(inRange)
    ranges.push_back({rangeStart, offset - rangeStart});

  // Coalesce ranges separated by small gaps
  if(gapThreshold > 0 && ranges.size() > 1)
  {
    std::vector<FetchRange> coalesced;
    coalesced.push_back(ranges[0]);
    for(size_t i = 1; i < ranges.size(); ++i)
    {
      auto& prev = coalesced.back();
      uint64_t gap = ranges[i].offset - prev.end();
      if(gap <= gapThreshold)
      {
        // Merge: extend previous range to cover gap + next range
        prev.length = ranges[i].end() - prev.offset;
      }
      else
      {
        coalesced.push_back(ranges[i]);
      }
    }
    return coalesced;
  }

  return ranges;
}

} // namespace grk
