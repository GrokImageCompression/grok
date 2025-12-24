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

namespace grk
{
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
      chunkMap[resno] = new std::unordered_map<size_t, uint8_t*>(); // Lazily allocate the map

    auto& chunks = *chunkMap[resno];
    uint64_t byteIndex = bitIndex >> 3; // Byte index within the resolution's buffer
    uint64_t chunkIndex =
        byteIndex / GRK_INCLUDE_TRACKER_CHUNK_SIZE; // Determine which chunk to access
    uint64_t chunkOffset = byteIndex % GRK_INCLUDE_TRACKER_CHUNK_SIZE; // Offset within the chunk

    // Lazily allocate the chunk
    if(!chunks.contains(chunkIndex))
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
  std::unordered_map<size_t, uint8_t*>* chunkMap[GRK_MAXRLVLS];
};

struct IncludeTracker
{
  IncludeTracker(void)
      : currentLayer(0), currentLayerIncludeBuf(nullptr),
        include(new std::unordered_map<uint16_t, LayerIncludeBuffers*>())
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
  uint16_t currentLayer;
  LayerIncludeBuffers* currentLayerIncludeBuf;
  std::unordered_map<uint16_t, LayerIncludeBuffers*>* include;
};

class PacketManager
{
public:
  PacketManager(bool compression, GrkImage* img, CodingParams* cparams, uint16_t tilenumber,
                T2_MODE t2_mode, TileProcessor* tileProc);
  virtual ~PacketManager();
  PacketIter* getPacketIter(uint32_t poc) const;
  /**
   Modify the packet iterator for enabling tile part generation
   @param prog_iter_num   	packet iterator number
   @param first_poc_tile_part true for first POC tile part
   @param newTilePartProgressionPosition 	The position of the tile part flag in the progression
   order
   */
  void enable_tile_part_generation(uint32_t prog_iter_num, bool first_poc_tile_part,
                                   uint8_t newTilePartProgressionPosition);
  /**
   * Updates the compressing parameters of the codec.
   *
   * @param	image		the image being encoded.
   * @param	p_cp		the coding parameters.
   * @param	tile_no	index of the tile being encoded.
   */
  static void updateCompressParams(const GrkImage* image, CodingParams* p_cp, TileCodingParams* tcp,
                                   uint16_t tile_no);

  IncludeTracker* getIncludeTracker(void);
  uint32_t getNumProgressions(void);
  TileProcessor* getTileProcessor(void);
  GrkImage* getImage();
  Rect32 getTileBounds(void);
  CodingParams* getCodingParams(void);
  T2_MODE getT2Mode(void);

private:
  /**
   * @brief Updates the coding parameters
   *
   * @param	tcp		@ref TileCodingParams
   * @param	num_comps		the number of components
   * @param	tileBounds tile bounds
   * @param	max_precincts	the maximum number of precincts for all the bands of the tile
   * @param	max_res	the maximum number of resolutions for all the poc inside the tile.
   * @param	dx_min		the minimum dx of all the components of all the resolutions for the
   * tile.
   * @param	dy_min		the minimum dy of all the components of all the resolutions for the
   * tile.
   * @param	poc		true if there is a progressio order change
   */
  static void updateCompressTcpProgressions(TileCodingParams* tcp, uint16_t num_comps,
                                            Rect32 tileBounds, uint64_t max_precincts,
                                            uint8_t max_res, uint32_t dx_min, uint32_t dy_min,
                                            bool poc);
  /**
   * Get the compression parameters needed to update the coding parameters and all the pocs.
   * The precinct widths, heights, dx and dy for each component at each resolution will be stored
   * as well. the last parameter of the function should be an array of pointers of size nb
   * components, each pointer leading to an area of size 4 * max_res. The data is stored inside
   * this area with the following pattern : dx_compi_res0 , dy_compi_res0 , w_compi_res0,
   * h_compi_res0 , dx_compi_res1 , dy_compi_res1 , w_compi_res1, h_compi_res1 , ...
   *
   * @param	image		the image being encoded.
   * @param	p_cp		the coding parameters.
   * @param	tileno		the tile index of the tile being encoded.
   * @param	tileBounds	tile bounds
   * @param	dx_min		minimum dx of all the components of all the resolutions for the tile.
   * @param	dy_min		minimum dy of all the components of all the resolutions for the tile.
   * @param	precincts	array of precincts
   * @param	max_precincts	maximum number of precincts for all the bands of the tile
   * @param	max_res		maximum number of resolutions for all the poc inside the tile.
   * @param	componentPrecinctInfo	stores precinct exponents and precinct grid dimensions
     for each component
   */
  static void getParams(const GrkImage* image, CodingParams* p_cp, TileCodingParams* tcp,
                        uint16_t tileno, Rect32* tileBounds, uint32_t* dx_min, uint32_t* dy_min,
                        IncludeTracker* includeTracker, uint64_t* max_precincts, uint8_t* max_res,
                        uint32_t** componentPrecinctInfo);
  GrkImage* image_;
  CodingParams* cp_;
  uint16_t tileIndex_;
  IncludeTracker* includeTracker_;
  PacketIter* pi_;
  T2_MODE t2Mode_;
  TileProcessor* tileProcessor_;
  Rect32 tileBounds_;
};

} // namespace grk
