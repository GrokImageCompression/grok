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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

#include <limits>

namespace grk
{
/**
 @file PacketIter.h
 @brief Implementation of a packet iterator (PI)

 A packet iterator gets the next packet following the progression order
*/

enum J2K_T2_MODE
{
  THRESH_CALC = 0, /** Function called in rate allocation process*/
  FINAL_PASS = 1 /** Function called in Tier 2 process*/
};

/**
 * @brief Chunk size for chunked resolution include buffer
 *
 */
constexpr size_t GRK_INCLUDE_TRACKER_CHUNK_SIZE = 1024;

/**
 * @struct LayerIncludeBuffers
 * @brief Include buffers for all resolutions in a given layer.
 * Each buffer is broken into chunks, and is lazy-allocated
 *
 */
struct LayerIncludeBuffers
{
  /**
   * @brief Construct a new LayerIncludeBuffers object
   *
   */
  LayerIncludeBuffers()
  {
    for(uint8_t i = 0; i < GRK_MAXRLVLS; ++i)
      chunkMap[i] = nullptr; // Buffers start as nullptr
  }

  /**
   * @brief Destroy the LayerIncludeBuffers object
   *
   */
  ~LayerIncludeBuffers()
  {
    clear();
  }

  /**
   * @brief Get the byte object
   * Lazily get or allocate a resolution's specific byte within its include buffer's matching
   * chunk
   *
   * @param resno resolution
   * @param precinctIndex compno * (num precincts for this resolution) + precinct number
   * @return uint8_t* pointer to byte
   */
  bool update(uint8_t resno, uint64_t bitIndex)
  {
    if(resno >= GRK_MAXRLVLS)
      throw std::out_of_range("Resolution index out of range");

    if(!chunkMap[resno])
      chunkMap[resno] = new std::map<size_t, uint8_t*>(); // Lazily allocate the map

    auto& chunks = *chunkMap[resno];
    uint64_t byteIndex = bitIndex >> 3; // Byte index within the resolution's buffer
    uint64_t chunkIndex =
        byteIndex / GRK_INCLUDE_TRACKER_CHUNK_SIZE; // Determine which chunk to access
    uint64_t chunkOffset = byteIndex % GRK_INCLUDE_TRACKER_CHUNK_SIZE; // Offset within the chunk

    // Lazily allocate the chunk
    if(chunks.find(chunkIndex) == chunks.end())
      chunks[chunkIndex] = new uint8_t[GRK_INCLUDE_TRACKER_CHUNK_SIZE](); // Allocate chunk lazily
    auto include = chunks[chunkIndex] + chunkOffset;
    uint8_t bit = (bitIndex & 7);
    uint8_t val = *include;
    if(((val >> bit) & 1) == 0)
    {
      *include = (uint8_t)(val | (1 << bit));
      return true;
    }

    return false;
  }

  /**
   * @brief Clears all chunks and chunkMaps
   *
   */
  void clear()
  {
    for(uint8_t i = 0; i < GRK_MAXRLVLS; ++i)
    {
      if(chunkMap[i])
      {
        for(auto& chunk : *chunkMap[i])
          delete[] chunk.second; // Delete each chunk
        delete chunkMap[i]; // Delete the map
        chunkMap[i] = nullptr;
      }
    }
  }

private:
  /**
   * @brief Lazily allocated maps of chunks for each resolution
   *
   */
  std::map<size_t, uint8_t*>* chunkMap[GRK_MAXRLVLS];
};

struct IncludeTracker
{
  IncludeTracker(uint16_t numcomponents)
      : numcomps(numcomponents), currentLayer(0), currentLayerIncludeBuf(nullptr),
        include(new std::map<uint16_t, LayerIncludeBuffers*>())
  {
    resetNumPrecinctsPerRes();
  }

  ~IncludeTracker()
  {
    clear();
    delete include;
  }

  bool update(uint16_t layno, uint8_t resno, uint16_t compno, uint64_t precno)
  {
    LayerIncludeBuffers* layerBuf = nullptr;

    // Retrieve or create the ResIncludeBuffers for the current layer
    if(layno == currentLayer && currentLayerIncludeBuf)
    {
      layerBuf = currentLayerIncludeBuf;
    }
    else
    {
      if(include->find(layno) == include->end())
        include->operator[](layno) = layerBuf = new LayerIncludeBuffers;
      else
        layerBuf = include->operator[](layno);
      currentLayerIncludeBuf = layerBuf;
      currentLayer = layno;
    }

    // Calculate the index in bits
    auto numprecs = numPrecinctsPerRes[resno];
    uint64_t bitIndex = compno * numprecs + precno;

    return layerBuf->update(resno, bitIndex);
  }

  void clear()
  {
    for(auto it = include->begin(); it != include->end(); ++it)
      delete it->second;
    include->clear();
  }

  void resetNumPrecinctsPerRes(void)
  {
    for(uint8_t i = 0; i < GRK_MAXRLVLS; ++i)
      numPrecinctsPerRes[i] = 0;
  }

  void updateNumPrecinctsPerRes(uint8_t resno, uint64_t numPrecincts)
  {
    if(numPrecincts > numPrecinctsPerRes[resno])
      numPrecinctsPerRes[resno] = numPrecincts;
  }

private:
  uint64_t numPrecinctsPerRes[GRK_MAXRLVLS];
  uint16_t numcomps;
  uint16_t currentLayer;
  LayerIncludeBuffers* currentLayerIncludeBuf;
  std::map<uint16_t, LayerIncludeBuffers*>* include;
};

class PacketManager;

/***
 * Cache state of a tile component's resolution
 * relative to the precinct grid in that resolution, and also
 * projected onto the tile's highest resolution (PRJ)
 */
struct ResPrecinctInfo
{
  ResPrecinctInfo();
  bool init(uint8_t resno, uint8_t decomplevel, grk_rect32 tileBounds, uint32_t dx, uint32_t dy,
            bool windowed, grk_rect32 tileWindow);
  void print(void);
  uint32_t precWidthExp;
  uint32_t precHeightExp;
  uint32_t precWidthExpPRJ;
  uint32_t precHeightExpPRJ;
  uint32_t resOffsetX0PRJ;
  uint32_t resOffsetY0PRJ;
  uint64_t precWidthPRJ;
  uint64_t precWidthPRJMinusOne;
  uint64_t precHeightPRJ;
  uint64_t precHeightPRJMinusOne;
  uint64_t numPrecincts_;
  uint64_t dxPRJ;
  uint64_t dyPRJ;
  uint32_t resInPrecGridX0;
  uint32_t resInPrecGridY0;
  uint8_t resno_;
  uint8_t decompLevel_;
  grk_rect32 tileBoundsPrecPRJ;
  grk_rect32 tileBoundsPrecGrid;
  grk_rect32 winPrecPRJ;
  grk_rect32 winPrecGrid;
  uint64_t innerPrecincts_;
  uint64_t winPrecinctsLeft_;
  uint64_t winPrecinctsRight_;
  uint64_t winPrecinctsTop_;
  uint64_t winPrecinctsBottom_;
  bool valid;
};

/***
 * Packet iterator resolution
 */
struct PiResolution
{
  PiResolution()
      : precWidthExp(0), precHeightExp(0), precinctGridWidth(0), precinctGridHeight(0),
        precinctInfo(nullptr)
  {}
  ~PiResolution()
  {
    delete precinctInfo;
  }
  uint32_t precWidthExp;
  uint32_t precHeightExp;
  uint32_t precinctGridWidth;
  uint32_t precinctGridHeight;
  ResPrecinctInfo* precinctInfo;
};

/**
 * Packet iterator component
 */
struct PiComp
{
  PiComp() : dx(0), dy(0), numresolutions(0), resolutions(nullptr) {}
  ~PiComp()
  {
    delete[] resolutions;
  }

  // component sub-sampling factors
  uint32_t dx;
  uint32_t dy;
  uint8_t numresolutions;
  PiResolution* resolutions;
};

/**
 Packet iterator
 */
struct PacketIter
{
  PacketIter();
  ~PacketIter();

  void init(PacketManager* packetMan, uint32_t pino, TileCodingParams* tcp, grk_rect32 tileBounds,
            bool compression, uint8_t max_res, uint64_t max_precincts,
            uint32_t* resolutionPrecinctGrid, uint32_t** precinctByComponent);

  void printStaticState(void);
  void printDynamicState(void);

  /**
    Modify the packet iterator for enabling tile part generation
    @param pino   	packet iterator number
    @param first_poc_tile_part true for first POC tile part
    @param tppos 	The position of the tile part flag in the progression order
    */
  void enable_tile_part_generation(uint32_t pino, bool first_poc_tile_part, uint32_t tppos);

  void genPrecinctInfo();
  void genPrecinctInfo(PiComp* comp, PiResolution* res, uint8_t resNumber);

  bool update_include(void);
  void destroy_include(void);

  /**
    Modify the packet iterator to point to the next packet
    @return false if pi pointed to the final packet, otherwise true
    */
  bool next(SparseBuffer* src);
  GRK_PROG_ORDER getProgression(void) const;
  uint16_t getCompno(void) const;
  uint8_t getResno(void) const;
  uint64_t getPrecinctIndex(void) const;
  uint16_t getLayno(void) const;

private:
  uint16_t compno;
  uint8_t resno;
  uint64_t precinctIndex;
  uint16_t layno;
  grk_progression prog;
  uint16_t numcomps;
  PiComp* comps;

  /** packet coordinates */
  uint64_t x, y;
  /** component sub-sampling */
  uint32_t dx, dy;
  uint32_t dxActive, dyActive;
  void update_dxy(void);
  bool checkForRemainingValidProgression(int32_t prog, uint32_t pino, const char* progString);
  // This packet iterator is designed so that the innermost progression
  // is only incremented before the **next** packet is processed.
  // i.e. it is not incremented before the very first packet is processed,
  // but rather before all subsequent packets are processed.
  // This flag keeps track of this state.
  bool incrementInner;

  PacketManager* packetManager;
  uint8_t maxNumDecompositionResolutions;
  bool singleProgression_;
  bool compression_;
  ResPrecinctInfo* precinctInfoOPT_;
  // precinct top,left grid coordinates
  uint32_t px0grid_;
  uint32_t py0grid_;
  bool skippedLeft_;
  bool genPrecinctY0Grid(ResPrecinctInfo* rpInfo);
  bool genPrecinctX0Grid(ResPrecinctInfo* rpInfo);
  bool precInfoCheck(ResPrecinctInfo* rpInfo);
  void generatePrecinctIndex(void);
  bool validatePrecinct(void);
  void update_dxy_for_comp(PiComp* comp, bool updateActive);
  bool isWholeTile(void);

  /**
    Get next packet in component-precinct-resolution-layer order.
    @return returns false if pi pointed to the final packet, otherwise true
    */
  bool next_cprl(SparseBuffer* src);
  bool next_cprlOPT(SparseBuffer* src);

  /**
    Get next packet in precinct-component-resolution-layer order.
    @return returns false if pi pointed to the final packet, otherwise true
    */
  bool next_pcrl(SparseBuffer* src);

  /**
    Get next packet in layer-resolution-component-precinct order.
    @return returns false if pi pointed to the final packet, otherwise true
    */
  bool next_lrcp(SparseBuffer* src);
  /**
    Get next packet in resolution-layer-component-precinct order.
    @return returns false if pi pointed to the final packet, otherwise true
    */
  bool next_rlcp(SparseBuffer* src);
  /**
    Get next packet in resolution-precinct-component-layer order.
    @return returns false if pi pointed to the final packet, otherwise true
    */
  bool next_rpcl(SparseBuffer* src);

  bool skipPackets(SparseBuffer* src, uint64_t numPackets);
};

} // namespace grk
