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
 * @brief Cached geometric state of a tile component's resolution, relative to its precinct grid.
 *
 * In JPEG 2000, each resolution level has its own precinct grid. When iterating packets
 * across resolutions (e.g. RPCL, CPRL, PCRL progression orders), lower-resolution precincts
 * must be projected onto the highest-resolution coordinate system so all resolutions share
 * a common iteration grid. Fields suffixed with "PRJ" represent values in this projected
 * (highest-resolution) coordinate space.
 *
 * ## Coordinate Spaces
 *
 * - **Resolution space**: native coordinates of a resolution level, after subsampling
 *   by `compDx * 2^decompLevel`. Precinct dimensions here are `2^precWidthExp` x `2^precHeightExp`.
 * - **Projected (PRJ) space**: coordinates projected back to the highest resolution.
 *   Precinct dimensions become `compDx * 2^(precWidthExp + decompLevel)` in this space.
 * - **Precinct grid**: integer grid where each cell represents one precinct. Obtained by
 *   dividing resolution-space coordinates by precinct dimensions.
 *
 * ## Overflow Protection
 *
 * `init()` returns false if projected precinct dimensions exceed `UINT32_MAX`, since
 * `Rect32::scale()` operates on `uint32_t` coordinates. This prevents silent truncation
 * that could produce degenerate tile bounds and infinite iteration loops.
 *
 * ## Windowed Decode
 *
 * When decoding a spatial sub-region (window), `winPrecGrid` and `winPrecPRJ` describe
 * the precinct bounds that intersect the decode window. The `winPrecincts{Left,Right,Top,Bottom}`
 * fields cache packet counts for precincts outside the window, enabling efficient skipping
 * via PLT markers.
 */
struct ResPrecinctInfo
{
  ResPrecinctInfo();

  /**
   * @brief Initializes resolution precinct info for a specific resolution level.
   *
   * Computes precinct grid dimensions, projected coordinates, and windowed decode bounds.
   * Returns false if the resolution has zero extent, or if projected precinct dimensions
   * would overflow uint32_t (preventing Rect32::scale() truncation bugs).
   *
   * @param resno           resolution number (0 = lowest)
   * @param decompLevel     decomposition level (numresolutions - 1 - resno)
   * @param tileBounds      tile bounds in image coordinates
   * @param dx              component horizontal subsampling factor
   * @param dy              component vertical subsampling factor
   * @param windowed        true if decoding a sub-region
   * @param tileWindow      decode window in unreduced tile coordinates (used only if windowed)
   * @return true on success, false if resolution is degenerate or overflows
   */
  bool init(uint8_t resno, uint8_t decompLevel, Rect32 tileBounds, uint32_t dx, uint32_t dy,
            bool windowed, Rect32 tileWindow);

  /**
   * @brief Prints resolution precinct info for debugging.
   */
  void print(void);

  /** log2 precinct width in resolution space (0-15, from codestream SPcoc) */
  uint8_t precWidthExp;
  /** log2 precinct height in resolution space */
  uint8_t precHeightExp;
  /** log2 precinct width in projected space: precWidthExp + decompLevel */
  uint8_t precWidthExpPRJ;
  /** log2 precinct height in projected space: precHeightExp + decompLevel */
  uint8_t precHeightExpPRJ;

  /** horizontal offset of resolution origin relative to projected precinct grid (zero when tile
   * origin is (0,0)) */
  uint32_t resOffsetX0PRJ;
  /** vertical offset of resolution origin relative to projected precinct grid */
  uint32_t resOffsetY0PRJ;

  /** precinct width in projected space: compDx << precWidthExpPRJ */
  uint64_t precWidthPRJ;
  /** precWidthPRJ - 1 (cached for bitwise alignment checks) */
  uint64_t precWidthPRJMinusOne;
  /** precinct height in projected space: compDy << precHeightExpPRJ */
  uint64_t precHeightPRJ;
  /** precHeightPRJ - 1 (cached for bitwise alignment checks) */
  uint64_t precHeightPRJMinusOne;

  /** total number of precincts in this resolution (= tileBoundsPrecGrid.area()) */
  uint64_t numPrecincts_;

  /** component subsampling projected to resolution: compDx << decompLevel */
  uint64_t dxPRJ;
  /** component subsampling projected to resolution: compDy << decompLevel */
  uint64_t dyPRJ;

  /** floor(res.x0 / 2^precWidthExp): resolution origin in precinct grid coords */
  uint32_t resInPrecGridX0;
  /** floor(res.y0 / 2^precHeightExp): resolution origin in precinct grid coords */
  uint32_t resInPrecGridY0;

  /** resolution number (0 = lowest) */
  uint8_t resno_;
  /** decomposition level (numresolutions - 1 - resno) */
  uint8_t decompLevel_;

  /** tile bounds snapped to precinct boundaries, in projected space */
  Rect32 tileBoundsPrecPRJ;
  /** tile bounds mapped to precinct grid (resolution space) */
  Rect32 tileBoundsPrecGrid;

  /** decode window bounds snapped to precinct boundaries, in projected space */
  Rect32 winPrecPRJ;
  /** decode window bounds mapped to precinct grid (resolution space) */
  Rect32 winPrecGrid;

  /** cached packet count: comp_e * lay_e (for RPCL skip optimization) */
  uint64_t innerPrecincts_;
  /** packets in precinct columns to the left of the decode window */
  uint64_t winPrecinctsLeft_;
  /** packets in precinct columns to the right of the decode window */
  uint64_t winPrecinctsRight_;
  /** packets in precinct rows above the decode window */
  uint64_t winPrecinctsTop_;
  /** packets in precinct rows below the decode window */
  uint64_t winPrecinctsBottom_;

  /** true if init() completed successfully */
  bool valid;
};

/**
 * @struct PacketIterInfoResolution
 * @brief Resolution-level precinct grid geometry for packet iteration.
 *
 * Stores the log2 precinct dimensions and the number of precincts in the grid
 * for one resolution of one component. Also optionally holds a pointer to a
 * fully computed @ref ResPrecinctInfo when the non-OPT code path is used.
 */
struct PacketIterInfoResolution
{
  PacketIterInfoResolution();
  ~PacketIterInfoResolution();

  /** log2 precinct width (from codestream SPcoc marker, 0-15) */
  uint8_t precWidthExp;
  /** log2 precinct height (from codestream SPcoc marker, 0-15) */
  uint8_t precHeightExp;
  /** number of precincts horizontally in this resolution's precinct grid */
  uint32_t precinctGridWidth;
  /** number of precincts vertically in this resolution's precinct grid */
  uint32_t precinctGridHeight;
  /** fully computed precinct info (non-OPT path only; null when precinctInfoOPT_ is used) */
  ResPrecinctInfo* precinctInfo;
};

/**
 * @struct PacketIterInfoComponent
 * @brief Component-level information for packet iteration.
 *
 * Holds the component's subsampling factors and an array of resolution-level
 * precinct grid information, one entry per resolution.
 */
struct PacketIterInfoComponent
{
  PacketIterInfoComponent();
  ~PacketIterInfoComponent();

  /** component horizontal subsampling factor (from SIZ marker) */
  uint32_t dx;
  /** component vertical subsampling factor (from SIZ marker) */
  uint32_t dy;
  /** number of resolution levels for this component (1 to GRK_MAXRLVLS) */
  uint8_t numresolutions;
  /** array of resolution-level precinct grid info, length = numresolutions */
  PacketIterInfoResolution* resolutions;
};

class PacketManager;

/**
 * @struct PacketIter
 * @brief Iterates through JPEG 2000 packets following a progression order.
 *
 * A JPEG 2000 codestream organizes compressed data into packets, each identified
 * by a (layer, resolution, component, precinct) tuple. The order in which packets
 * appear is determined by the progression order (LRCP, RLCP, RPCL, PCRL, or CPRL).
 *
 * This iterator generates the packet sequence for a given progression order,
 * supporting both compression and decompression, with optional windowed (sub-region)
 * decode and progression order changes (POC).
 *
 * ## Optimized vs Non-Optimized Paths
 *
 * When decompressing under common conditions, an optimized code path (methods
 * suffixed with OPT) is used. The OPT path caches all resolution precinct info
 * in `precinctInfoOPT_` and avoids per-packet recomputation. The optimization
 * conditions are:
 * 1. Single progression (no POC)
 * 2. No subsampling (all components have dx=dy=1)
 * 3. Constant number of resolutions across all components
 * 4. Non-decreasing projected precinct size as resolution decreases (CPRL/PCRL only)
 * 5. Tile origin at (0,0)
 *
 * ## Spatial Progressions and Step Sizes
 *
 * PCRL, RPCL, and CPRL iterate over spatial (x,y) coordinates using step sizes
 * `dx`/`dy` computed from component subsampling and precinct dimensions. If all
 * resolution precinct step sizes exceed UINT32_MAX, dx/dy remain 0 and `next()`
 * returns false immediately to prevent infinite loops.
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
  /** current component number in iteration */
  uint16_t compno = 0;
  /** current resolution number in iteration */
  uint8_t resno = 0;
  /** current precinct index in iteration */
  uint64_t precinctIndex = 0;
  /** current layer number in iteration */
  uint16_t layno = 0;
  /** progression order and iteration bounds */
  grk_progression prog;
  /** number of image components */
  uint16_t numcomps = 0;
  /** per-component precinct grid info, length = numcomps */
  PacketIterInfoComponent* comps = nullptr;

  /** current x position in spatial progression (projected coordinates) */
  uint64_t x = 0;
  /** current y position in spatial progression (projected coordinates) */
  uint64_t y = 0;
  /**
   * Minimum horizontal step size across all components/resolutions for spatial
   * progressions (PCRL, RPCL, CPRL). Computed as the smallest projected precinct
   * width that fits in uint32_t. Zero if all projected precinct widths exceed UINT_MAX.
   */
  uint32_t dx = 0;
  /**
   * Minimum vertical step size across all components/resolutions for spatial
   * progressions. Same semantics as dx.
   */
  uint32_t dy = 0;
  /** active horizontal step (accounts for alignment to current x position) */
  uint32_t dxActive = 0;
  /** active vertical step (accounts for alignment to current y position) */
  uint32_t dyActive = 0;

  /**
   * @brief Computes dx/dy step sizes for all components.
   */
  void update_dxy(void);

  /**
   * @brief Checks if there is a remaining valid progression for tile part generation.
   */
  bool checkForRemainingValidProgression(int32_t prog, uint32_t prog_iter_num,
                                         const char* progString);
  // This packet iterator is designed so that the innermost progression
  // is only incremented before the **next** packet is processed.
  // i.e. it is not incremented before the very first packet is processed,
  // but rather before all subsequent packets are processed.
  // This flag keeps track of this state.
  bool incrementInner = false;

  /** owning packet manager */
  PacketManager* packetManager = nullptr;
  /** max number of decomposition resolutions to decompress */
  uint8_t maxNumDecompositionResolutions = 0;
  /** true if there is exactly one progression (no POC markers) */
  bool singleProgression_ = false;
  /** true when used for compression, false for decompression */
  bool compression_ = false;
  /**
   * Cached per-resolution precinct info for the optimized iteration path.
   * Non-null only when all OPT preconditions are met. Length = comps[0].numresolutions.
   */
  ResPrecinctInfo* precinctInfoOPT_ = nullptr;

  /** precinct grid x-coordinate of current precinct's top-left corner */
  uint32_t px0grid_ = 0;
  /** precinct grid y-coordinate of current precinct's top-left corner */
  uint32_t py0grid_ = 0;

  /** RPCL OPT: tracks whether left-of-window precincts have been skipped for current row */
  bool skippedLeft_ = false;

  /**
   * @brief Computes py0grid_ for the current (x,y) position (non-OPT path).
   * @return false if current y is not aligned to a precinct boundary
   */
  bool genPrecinctY0Grid(ResPrecinctInfo* rpInfo);
  /**
   * @brief Computes px0grid_ for the current (x,y) position (non-OPT path).
   * @return false if current x is not aligned to a precinct boundary
   */
  bool genPrecinctX0Grid(ResPrecinctInfo* rpInfo);
  /** @brief Computes py0grid_ for RPCL OPT path (tile origin at (0,0), no subsampling). */
  void genPrecinctY0GridRPCL_OPT(ResPrecinctInfo* rpInfo);
  /** @brief Computes px0grid_ for RPCL OPT path. */
  void genPrecinctX0GridRPCL_OPT(ResPrecinctInfo* rpInfo);
  /**
   * @brief Computes px0grid_ for PCRL/CPRL OPT path.
   * @return false if current x is not aligned to a precinct boundary
   */
  bool genPrecinctX0GridPCRL_OPT(ResPrecinctInfo* rpInfo);
  /**
   * @brief Computes py0grid_ for PCRL/CPRL OPT path.
   * @return false if current y is not aligned to a precinct boundary
   */
  bool genPrecinctY0GridPCRL_OPT(ResPrecinctInfo* rpInfo);
  /**
   * @brief Validates that the current (compno, resno, x, y) maps to a valid precinct.
   * @return false if the precinct is invalid or degenerate
   */
  bool precInfoCheck(ResPrecinctInfo* rpInfo);
  /** @brief Computes precinctIndex from (px0grid_, py0grid_) and precinct grid width. */
  void generatePrecinctIndex(void);
  /**
   * @brief Validates the current precinct and computes grid coordinates.
   * @return true if precinct is valid, false otherwise (skip this packet position)
   */
  bool validatePrecinct(void);
  /**
   * @brief Computes dx/dy for a single component and merges into the global minimum.
   * @param comp component info
   * @param updateActive if true, also updates dxActive/dyActive
   */
  void update_dxy_for_comp(PacketIterInfoComponent* comp, bool updateActive);
  /**
   * @brief Returns true if the full tile is being decoded (no windowed sub-region).
   */
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
