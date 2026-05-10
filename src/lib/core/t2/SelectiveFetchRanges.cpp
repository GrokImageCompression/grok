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

namespace
{
template<typename T>
T ceildivpow2_local(T a, uint8_t b)
{
  return (T)((a + ((uint64_t)1 << b) - 1) >> b);
}
inline uint32_t floordivpow2_local(uint32_t a, uint8_t b)
{
  return a >> b;
}
} // namespace

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

TilePartHeaderInfo extractTilePartHeaderInfo(const uint8_t* headerData, size_t headerSize)
{
  TilePartHeaderInfo result{};
  result.valid = false;

  if(!headerData || headerSize < 4)
    return result;

  // Marker IDs
  constexpr uint16_t PLT_MARKER = 0xFF58;
  constexpr uint16_t SOD_MARKER = 0xFF93;

  size_t pos = 0;

  // Scan for markers. Each marker is 0xFF XX, followed by a 2-byte big-endian length
  // (except SOD which has no length field — data follows immediately).
  while(pos + 1 < headerSize)
  {
    // Look for marker prefix
    if(headerData[pos] != 0xFF)
    {
      pos++;
      continue;
    }

    uint16_t markerId = ((uint16_t)headerData[pos] << 8) | headerData[pos + 1];

    if(markerId == SOD_MARKER)
    {
      result.sodOffset = pos;
      result.valid = true;
      return result;
    }

    // All other markers have a 2-byte length field after the marker ID
    if(pos + 3 >= headerSize)
      break; // not enough data for length field

    uint16_t markerLen = ((uint16_t)headerData[pos + 2] << 8) | headerData[pos + 3];

    if(markerId == PLT_MARKER)
    {
      // PLT body starts at pos+4, length is markerLen-2 (excluding length field itself)
      // First byte after length is Zplt (index), rest are VBR-encoded packet lengths
      if(pos + 4 >= headerSize || markerLen < 3)
      {
        pos += 2 + markerLen;
        continue;
      }

      // Skip Zplt byte
      size_t pltStart = pos + 5; // pos+2 (len field) + 2 (len value) + 1 (Zplt)
      size_t pltEnd = pos + 2 + markerLen;
      if(pltEnd > headerSize)
        pltEnd = headerSize;

      // Decode VBR packet lengths
      uint32_t packetLen = 0;
      for(size_t i = pltStart; i < pltEnd; ++i)
      {
        uint8_t byte = headerData[i];
        packetLen |= (byte & 0x7F);
        if(byte & 0x80)
        {
          packetLen <<= 7;
        }
        else
        {
          result.pltLengths.push_back(packetLen);
          packetLen = 0;
        }
      }
    }

    // Advance past this marker: marker_id(2) + length(markerLen includes length field bytes)
    pos += 2 + markerLen;
  }

  // If we reach here, SOD was not found
  return result;
}

uint64_t computeNumPrecincts(uint32_t tcx0, uint32_t tcy0, uint32_t tcx1, uint32_t tcy1,
                             uint8_t numResolutions, uint8_t resno, uint8_t precWidthExp,
                             uint8_t precHeightExp)
{
  // Scale tile-component bounds to resolution level
  uint8_t power = (uint8_t)(numResolutions - 1U - resno);
  uint32_t rx0 = ceildivpow2_local(tcx0, power);
  uint32_t ry0 = ceildivpow2_local(tcy0, power);
  uint32_t rx1 = ceildivpow2_local(tcx1, power);
  uint32_t ry1 = ceildivpow2_local(tcy1, power);

  uint32_t resW = (rx1 > rx0) ? (rx1 - rx0) : 0;
  uint32_t resH = (ry1 > ry0) ? (ry1 - ry0) : 0;

  if(resW == 0 || resH == 0)
    return 0;

  // Compute precinct grid
  uint32_t adjX0 = floordivpow2_local(rx0, precWidthExp) << precWidthExp;
  uint32_t adjY0 = floordivpow2_local(ry0, precHeightExp) << precHeightExp;
  uint32_t adjX1 = ceildivpow2_local(rx1, precWidthExp) << precWidthExp;
  uint32_t adjY1 = ceildivpow2_local(ry1, precHeightExp) << precHeightExp;

  uint32_t gridW = (adjX1 - adjX0) >> precWidthExp;
  uint32_t gridH = (adjY1 - adjY0) >> precHeightExp;

  return (uint64_t)gridW * gridH;
}

} // namespace grk
