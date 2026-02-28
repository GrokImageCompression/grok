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

#include <limits>

namespace grk
{

enum T2_MODE
{
  THRESH_CALC = 0, /** Function called in rate allocation process*/
  FINAL_PASS = 1 /** Function called in Tier 2 process*/
};

/**
 * @struct ResPrecinctInfo
 * @brief cache state of tile component's resolution
 *
 * State is relative to the precinct grid in that resolution, and also
 * projected onto the tile's highest resolution (indicated by PRJ)
 */
struct ResPrecinctInfo
{
  ResPrecinctInfo();
  bool init(uint8_t resno, uint8_t decomplevel, Rect32 tileBounds, uint32_t dx, uint32_t dy,
            bool windowed, Rect32 tileWindow);
  void print(void);
  uint8_t precWidthExp;
  uint8_t precHeightExp;
  uint8_t precWidthExpPRJ;
  uint8_t precHeightExpPRJ;
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
  Rect32 tileBoundsPrecPRJ;
  Rect32 tileBoundsPrecGrid;
  Rect32 winPrecPRJ;
  Rect32 winPrecGrid;
  uint64_t innerPrecincts_;
  uint64_t winPrecinctsLeft_;
  uint64_t winPrecinctsRight_;
  uint64_t winPrecinctsTop_;
  uint64_t winPrecinctsBottom_;
  bool valid;
};

/**
 * @struct PacketIterInfoResolution
 * @brief Resolution level information for packet iterator
 */
struct PacketIterInfoResolution
{
  PacketIterInfoResolution();
  ~PacketIterInfoResolution();
  uint8_t precWidthExp;
  uint8_t precHeightExp;
  uint32_t precinctGridWidth;
  uint32_t precinctGridHeight;
  ResPrecinctInfo* precinctInfo;
};

/**
 * @struct PacketIterInfoComponent
 * @brief Component level information for packet iterator
 */
struct PacketIterInfoComponent
{
  PacketIterInfoComponent();
  ~PacketIterInfoComponent();

  // component sub-sampling factors
  uint32_t dx;
  uint32_t dy;
  uint8_t numresolutions;
  PacketIterInfoResolution* resolutions;
};

class PacketManager;

/**
 * @struct PacketIter
 * @brief iterates through packets following progression order
 *
 * When decompressing under certain certain common conditions,
 * iteration has been optimized:
 * These conditions are :
 * 1. single progression
 * 2. no subsampling
 * 3. constant number of resolutions across components
 * 4. non-decreasing projected precinct size as resolution decreases (CPRL and PCRL)
 * 5. tile origin at (0,0)
 *
 */
struct PacketIter
{
  /**
   * @brief Constructs PacketIter
   */
  PacketIter();
  /**
   * @brief Destroys PacketIter
   */
  ~PacketIter();

  /**
   * @brief Initializes PacketItr
   * @param packetMan @ref PacketManager
   * @param pocIndex index into array of progression order changes
   * @param tcp @ref TileCodingParams
   * @param tileBounds tile bounds
   * @param compression true if the iterator is for the compressor
   * @param max_res  maximum number of resolutions across all components
   * @param max_precints maximum number of precincts, across all components
   * @param componentPrecinctInfo precinct info for each component
   */
  void init(PacketManager* packetMan, uint32_t pocIndex, TileCodingParams* tcp, Rect32 tileBounds,
            bool compression, uint8_t max_res, uint64_t max_precincts,
            uint32_t** componentPrecinctInfo);

  /**
   * @brief Prints static debug state of iterator
   */
  void printStaticState(void);

  /**
   * @brief Prints dynamic debug state of the iterator
   */
  void printDynamicState(void);

  /**
   * @brief Modifies the packet iterator to enable tile part generation
   * @param prog_iter_num   	packet iterator number
   * @param first_poc_tile_part true for first POC tile part
   * @param newTilePartProgressionPosition 	position of tile part flag in the progression order
   *
   */
  void enable_tile_part_generation(uint32_t prog_iter_num, bool first_poc_tile_part,
                                   uint8_t newTilePartProgressionPosition);

  /**
   * @brief Generates optimized precinct information
   * @return true if image meets optimization criteria
   */
  bool genPrecinctInfoOPT();

  /**
   * @brief Generates non-optimized precinct information
   */
  void genPrecinctInfo();

  /**
   * @brief Generates precinct information
   * @param comp @ref PacketIterInfoComponent
   * @param res @ref PacketIterInfoResolution
   * @param resNumber resolution number
   */
  void genPrecinctInfo(PacketIterInfoComponent* comp, PacketIterInfoResolution* res,
                       uint8_t resNumber);

  /**
   * @brief Gets point to packet include information i.e. whether packet has already
   * been generated or not
   * @param layerIndex layer index
   * @return ponter to packet include information
   */
  uint8_t* get_include(uint16_t layerIndex);

  /**
   * @brief Updates include state for current packet
   * @return true if successful
   */
  bool update_include(void);

  /**
   * @brief Clears all include states
   */
  void destroy_include(void);

  /**
   * @brief Moves to next packet
   * @param compressedPackets @ref SparseBuffer holding packet
   * This parameter is only non-null when there are PLT markers
   * @return false if packet iterator points to final packet, otherwise true
   */
  bool next(SparseBuffer* compressedPackets);

  /**
   * @brief Gets current progression order
   * @return @ref GRK_PROG_ORDER
   */
  GRK_PROG_ORDER getProgression(void) const;

  /**
   * @brief Gets component number for iterator's current packet
   * @return component number
   */
  uint16_t getCompno(void) const;

  /**
   * @brief Gets resolution number for iterator's current packet
   * @return resolution number
   */
  uint8_t getResno(void) const;

  /**
   * @brief Gets precinct index for iterator's current packet
   * @return precinct index
   */
  uint64_t getPrecinctIndex(void) const;

  /**
   * @brief Gets layer number for iterator's current packet
   * @return layer number
   */
  uint16_t getLayno(void) const;

private:
  uint16_t compno = 0;
  uint8_t resno = 0;
  uint64_t precinctIndex = 0;
  uint16_t layno = 0;
  grk_progression prog;
  uint16_t numcomps = 0;
  PacketIterInfoComponent* comps = nullptr;

  /** packet coordinates */
  uint64_t x = 0, y = 0;
  /** component sub-sampling */
  uint32_t dx = 0, dy = 0;
  uint32_t dxActive = 0, dyActive = 0;
  void update_dxy(void);
  bool checkForRemainingValidProgression(int32_t prog, uint32_t prog_iter_num,
                                         const char* progString);
  // This packet iterator is designed so that the innermost progression
  // is only incremented before the **next** packet is processed.
  // i.e. it is not incremented before the very first packet is processed,
  // but rather before all subsequent packets are processed.
  // This flag keeps track of this state.
  bool incrementInner = false;

  PacketManager* packetManager = nullptr;
  uint8_t maxNumDecompositionResolutions = 0;
  bool singleProgression_ = false;
  bool compression_ = false;
  ResPrecinctInfo* precinctInfoOPT_ = nullptr;

  // precinct top,left grid coordinates
  uint32_t px0grid_ = 0;
  uint32_t py0grid_ = 0;

  bool skippedLeft_ = false;
  bool genPrecinctY0Grid(ResPrecinctInfo* rpInfo);
  bool genPrecinctX0Grid(ResPrecinctInfo* rpInfo);
  void genPrecinctY0GridRPCL_OPT(ResPrecinctInfo* rpInfo);
  void genPrecinctX0GridRPCL_OPT(ResPrecinctInfo* rpInfo);
  bool genPrecinctX0GridPCRL_OPT(ResPrecinctInfo* rpInfo);
  bool genPrecinctY0GridPCRL_OPT(ResPrecinctInfo* rpInfo);
  bool precInfoCheck(ResPrecinctInfo* rpInfo);
  void generatePrecinctIndex(void);
  bool validatePrecinct(void);
  void update_dxy_for_comp(PacketIterInfoComponent* comp, bool updateActive);
  bool isWholeTile(void);

  /**
   Get next packet in component-precinct-resolution-layer order.
   @return returns false if pi pointed to the final packet, otherwise true
   */
  bool next_cprl(SparseBuffer* compressedPackets);
  bool next_cprlOPT(SparseBuffer* compressedPackets);

  /**
   Get next packet in precinct-component-resolution-layer order.
   @return returns false if pi pointed to the final packet, otherwise true
   */
  bool next_pcrl();
  bool next_pcrlOPT();

  /**
   Get next packet in layer-resolution-component-precinct order.
   @return returns false if pi pointed to the final packet, otherwise true
   */
  bool next_lrcp();
  bool next_lrcpOPT();
  /**
   Get next packet in resolution-layer-component-precinct order.
   @return returns false if pi pointed to the final packet, otherwise true
   */
  bool next_rlcp();
  bool next_rlcpOPT();
  /**
   Get next packet in resolution-precinct-component-layer order.
   @return returns false if pi pointed to the final packet, otherwise true
   */
  bool next_rpcl(SparseBuffer* compressedPackets);
  bool next_rpclOPT(SparseBuffer* compressedPackets);

  bool skipPackets(SparseBuffer* compressedPackets, uint64_t numPackets);
};

} // namespace grk
