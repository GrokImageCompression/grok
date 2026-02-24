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

#include "geometry.h"
#include "PacketProgressionState.h"
#include "PostDecodeFilters.h"
#include "htconfig.h"
#include "SparseCanvas.h"

const bool DEBUG_TILE_COMPONENT = false;

namespace grk
{

/**
 * @struct TileComponent
 * @brief Stores sub-sampled, unreduced tile component dimensions,
 * along with reduction information. Also manages @ref TileComponentWindow
 */
struct TileComponent : public Rect32
{
  /**
   * @brief Constructs a new Tile Component object
   *
   */
  TileComponent()
      : resolutions_(nullptr), num_resolutions_(0), resolutions_to_decompress_(0),
        regionWindow_(nullptr), wholeTileDecompress_(true), isCompressor_(false), window_(nullptr),
        tccp_(nullptr)
  {}
  /**
   * @brief Destroys the Tile Component object
   *
   */

  ~TileComponent()
  {
    if(resolutions_)
    {
      for(auto resno = 0U; resno < num_resolutions_; ++resno)
      {
        const auto res = resolutions_ + resno;
        for(auto bandIndex = 0U; bandIndex < 3; ++bandIndex)
        {
          auto band = res->band + bandIndex;
          for(auto prc : band->precincts_)
            delete prc;
          band->precincts_.clear();
        }
      }
      delete[] resolutions_;
    }
    dealloc();
  }

  /**
   * @brief Allocates a region window
   *
   * @param numres number of resolutions
   * @param truncatedTile true if tile was truncated
   * @return true if successful
   */
  bool allocRegionWindow(uint32_t numres, bool truncatedTile)
  {
    Rect32 temp(0, 0, 0, 0);
    bool first = true;

    // 1. find outside bounds of all relevant code blocks, in relative coordinates
    for(uint8_t resno = 0U; resno < numres; ++resno)
    {
      const auto res = &resolutions_[resno];
      for(auto bandIndex = 0U; bandIndex < res->numBands_; ++bandIndex)
      {
        auto band = res->band + bandIndex;
        auto roi = window_->getBandWindowPadded(resno, band->orientation_);
        for(auto precinct : band->precincts_)
        {
          if(precinct->empty())
            continue;
          auto cblk_grid = precinct->getCblkGrid();
          auto cblk_expn = precinct->getCblkExpn();
          auto roi_grid = roi->scaleDownPow2(cblk_expn).clip(cblk_grid);
          auto w = cblk_grid.width();
          for(auto j = roi_grid.y0; j < roi_grid.y1; ++j)
          {
            uint32_t cblkno =
                (uint32_t)((roi_grid.x0 - cblk_grid.x0()) + (uint64_t)(j - cblk_grid.y0()) * w);
            for(auto i = roi_grid.x0; i < roi_grid.x1; ++i)
            {
              auto cblkBounds = precinct->getCodeBlockBounds(cblkno);

              // transform from canvas coordinates
              // to buffer coordinates (relative to associated resolution origin)
              uint32_t x = cblkBounds.x0() - band->x0;
              uint32_t y = cblkBounds.y0() - band->y0;
              if(band->orientation_ & 1)
              {
                auto prev_res = resolutions_ + resno - 1;
                x += prev_res->width();
              }
              if(band->orientation_ & 2)
              {
                auto prev_res = resolutions_ + resno - 1;
                y += prev_res->height();
              }
              // add to union of code block bounds
              if(first)
              {
                temp = Rect32(x, y, x + cblkBounds.width(), y + cblkBounds.height());
                first = false;
              }
              else
              {
                temp =
                    temp.rectUnion(Rect32(x, y, x + cblkBounds.width(), y + cblkBounds.height()));
              }
              cblkno++;
            }
          }
        }
      }
    }

    // 2. create (padded) sparse canvas, in buffer space,
    const uint32_t blockSizeExp = 6;
    temp.grow_IN_PLACE(8);
    auto regionWindow = new SparseCanvas<int32_t, blockSizeExp, blockSizeExp>(temp);

    // 3. allocate sparse blocks
    for(uint8_t resno = 0; resno < numres; ++resno)
    {
      const auto res = resolutions_ + resno;
      for(auto bandIndex = 0U; bandIndex < res->numBands_; ++bandIndex)
      {
        auto band = res->band + bandIndex;
        auto roi = window_->getBandWindowPadded(resno, band->orientation_);
        for(auto precinct : band->precincts_)
        {
          if(precinct->empty())
            continue;
          auto cblk_grid = precinct->getCblkGrid();
          auto cblk_expn = precinct->getCblkExpn();
          auto roi_grid = roi->scaleDownPow2(cblk_expn).clip(cblk_grid);
          auto w = cblk_grid.width();
          for(auto gridY = roi_grid.y0; gridY < roi_grid.y1; ++gridY)
          {
            uint32_t cblkno =
                (uint32_t)((roi_grid.x0 - cblk_grid.x0()) + (uint64_t)(gridY - cblk_grid.y0()) * w);
            for(auto gridX = roi_grid.x0; gridX < roi_grid.x1; ++gridX)
            {
              auto cblkBounds = precinct->getCodeBlockBounds(cblkno);

              // transform from canvas coordinates
              // to buffer coordinates (relative to associated resolution origin)
              uint32_t x = cblkBounds.x0() - band->x0;
              uint32_t y = cblkBounds.y0() - band->y0;
              if(band->orientation_ & 1)
              {
                auto prev_res = resolutions_ + resno - 1;
                x += prev_res->width();
              }
              if(band->orientation_ & 2)
              {
                auto prev_res = resolutions_ + resno - 1;
                y += prev_res->height();
              }

              if(!regionWindow->alloc(Rect32(x, y, x + cblkBounds.width(), y + cblkBounds.height()),
                                      truncatedTile))
              {
                delete regionWindow;
                throw std::runtime_error("unable to allocate sparse array");
              }
              cblkno++;
            }
          }
        }
      }
    }

    if(regionWindow_)
      delete regionWindow_;
    regionWindow_ = regionWindow;

    return true;
  }

  /**
   * @brief Check if a window can be created
   *
   * @param unreducedTileCompOrImageCompWindow window bounds
   * @return true if successful
   */
  bool canCreateWindow(Rect32 windowBounds)
  {
    auto maxResolution = resolutions_ + num_resolutions_ - 1;
    if(!maxResolution->intersection(windowBounds).valid())
    {
      grklog.error("Decompress region (%u,%u,%u,%u) must overlap image bounds (%u,%u,%u,%u)",
                   windowBounds.x0, windowBounds.y0, windowBounds.x1, windowBounds.y1,
                   maxResolution->x0, maxResolution->y0, maxResolution->x1, maxResolution->y1);
      return false;
    }

    return true;
  }

  /**
   * @brief Creates tile component window.
   *
   * Compression: unreduced, unsubsampled, full size tile component
   * Decompresssion: unreduced, subsampled, windowed image component
   *
   * @param unreducedTileCompOrImageCompWindow window bounds
   */
  void createWindow(Rect32 unreducedTileCompOrImageCompWindow)
  {
    dealloc();
    window_ = new TileComponentWindow<int32_t>(
        isCompressor_, tccp_->qmfbid_ == 1, wholeTileDecompress_,
        resolutions_ + num_resolutions_ - 1, this, unreducedTileCompOrImageCompWindow,
        num_resolutions_, isCompressor_ ? num_resolutions_ : resolutions_to_decompress_);
  }

  /**
   * @brief Deallocates component resources
   *
   */
  void dealloc(void)
  {
    delete regionWindow_;
    regionWindow_ = nullptr;
    delete window_;
    window_ = nullptr;
  }

  /**
   * @brief Initalizes tile component
   *
   * @param tileProcessor @ref TileProcessor
   * @param unreducedTileComp unreduced bounds
   * @param prec precision
   * @param tccp @ref TileComponentCodingParams
   */
  /**
   * Initialize tile component in unreduced tile component coordinates
   * (tile component coordinates take sub-sampling into account).
   *
   */
  void init(Resolution* resolutions, bool isCompressor, bool wholeTileDecompress, uint8_t reduce,
            TileComponentCodingParams* tccp)
  {
    delete[] resolutions_;
    resolutions_ = resolutions;
    num_resolutions_ = tccp->numresolutions_;
    currentPacketProgressionState_ = PacketProgressionState(num_resolutions_);
    nextPacketProgressionState_ = currentPacketProgressionState_;
    isCompressor_ = isCompressor;
    if(!isCompressor_)
      wholeTileDecompress_ = wholeTileDecompress;
    tccp_ = tccp;
    update(reduce);
  }

  /**
   * @brief Differential decompression update
   *
   * @param tileProcessor @ref TileProcessor
   */

  void update(uint8_t reduce)
  {
    resolutions_to_decompress_ =
        tccp_->numresolutions_ < reduce ? 1 : (uint8_t)(tccp_->numresolutions_ - reduce);
    auto highestNumberOfResolutions =
        (!isCompressor_) ? resolutions_to_decompress_ : num_resolutions_;
    auto hightestResolution = resolutions_ + highestNumberOfResolutions - 1;
    setRect(hightestResolution);
  }

  /**
   * @brief Gets window
   *
   * @return TileComponentWindow<int32_t>*
   */
  TileComponentWindow<int32_t>* getWindow() const
  {
    return window_;
  }
  /**
   * @brief Checks if whole tile will be decoded
   *
   * @return true if whole tile will be decoded
   */
  bool isWholeTileDecoding()
  {
    return wholeTileDecompress_;
  }
  /**
   * @brief Gets return window
   *
   * @return ISparseCanvas*
   */
  ISparseCanvas<int32_t>* getRegionWindow()
  {
    return regionWindow_;
  }
  /**
   * @brief Post processes code block
   *
   * @param srcData source data
   * @param block @ref DecompressBlockExec
   */
  template<typename T>
  void postProcess(T* srcData, t1::DecompressBlockExec* block)
  {
    if(block->roishift)
    {
      if(block->qmfbid == 1)
        postDecompressImpl<T, t1::RoiShiftFilter<T>>(srcData, block,
                                                     (uint16_t)block->cblk->width());
      else
        postDecompressImpl<T, t1::RoiScaleFilter<T>>(srcData, block,
                                                     (uint16_t)block->cblk->width());
    }
    else
    {
      if(block->qmfbid == 1)
        postDecompressImpl<T, t1::ShiftFilter<T>>(srcData, block, (uint16_t)block->cblk->width());
      else
        postDecompressImpl<T, t1::ScaleFilter<T>>(srcData, block, (uint16_t)block->cblk->width());
    }
  }

  /**
   * @brief Post processes HTJ2K code block
   *
   * @param srcData source data
   * @param block @ref DecompressBlockExec
   * @param stride source data stride
   */
  template<typename T>
  void postProcessHT(T* srcData, t1::DecompressBlockExec* block, uint16_t stride)
  {
    if(block->roishift)
    {
      if(block->qmfbid == 1)
        postDecompressImpl<T, t1::ojph::RoiShiftOJPHFilter<T>>(srcData, block, stride);
      else
        postDecompressImpl<T, t1::ojph::RoiScaleOJPHFilter<T>>(srcData, block, stride);
    }
    else
    {
      if(block->qmfbid == 1)
        postDecompressImpl<T, t1::ojph::ShiftOJPHFilter<T>>(srcData, block, stride);
      else
        postDecompressImpl<T, t1::ojph::ScaleOJPHFilter<T>>(srcData, block, stride);
    }
  }

  /**
   * @brief array of @ref Resolution
   *
   */
  Resolution* resolutions_; // in canvas coordinates
  /**
   * @brief number of resolutions
   *
   */
  uint8_t num_resolutions_;
  /**
   * @brief number of desired resolutions to decompress
   *
   */
  uint8_t resolutions_to_decompress_;
  /**
   * @brief current @ref PacketProgressionState
   *
   */
  PacketProgressionState currentPacketProgressionState_;
  /**
   * @brief next @ref PacketProgressionState
   *
   */
  PacketProgressionState nextPacketProgressionState_;

private:
  /**
   * @brief Implements post decompress filter
   *
   * @tparam F filter
   * @param srcData source data
   * @param block @DecompressBlockExec
   * @param stride source data stride
   */
  template<typename T, typename F>
  void postDecompressImpl(T* srcData, t1::DecompressBlockExec* block, uint16_t stride)
  {
    auto cblk = block->cblk;
    bool empty = cblk->dataChunksEmpty();

    uint32_t x = block->x;
    uint32_t y = block->y;
    window_->toRelativeCoordinates(block->resno, block->bandOrientation, x, y);
    auto src = Buffer2d<T, AllocatorAligned>(srcData, false, cblk->width(), stride, cblk->height());
    auto blockBounds = Rect32(x, y, x + cblk->width(), y + cblk->height());
    if(!empty)
    {
      if(regionWindow_)
      {
        src.template copyFrom<F>(src, F{block}); // Create functor with block
      }
      else
      {
        src.setRect(blockBounds);
        window_->postProcess<F>(src, block->resno, block->bandOrientation, block);
      }
    }
    if(regionWindow_)
      regionWindow_->write(block->resno, blockBounds, empty ? nullptr : srcData, 1,
                           blockBounds.width());
  }
  /**
   * @brief @ISparseCanvas for region window
   *
   */
  ISparseCanvas<int32_t>* regionWindow_;
  /**
   * @brief true if whole tile will be decompressed
   *
   */
  bool wholeTileDecompress_;
  /**
   * @brief true if compression is occcurring
   *
   */
  bool isCompressor_;
  /**
   * @brief @ref TileComponentWindow
   *
   */
  TileComponentWindow<int32_t>* window_;
  /**
   * @brief @ref TileComponentCodingParams
   *
   */
  TileComponentCodingParams* tccp_;
};

} // namespace grk
