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

#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <cstdint>
#include <optional>
#include <vector>
#include <functional>

#include "MinHeap.h"

namespace grk
{

class TileCompletion
{
public:
  // Callback type for when a full row of tiles is completed
  using RowCompletionCallback = std::function<void(uint16_t tileIndexBegin, uint16_t tileIndexEnd)>;

  // Constructor for full region or subregion
  TileCompletion(TileCache* tileCache, const Rect32& imageBounds, uint32_t tileWidth,
                 uint32_t tileHeight, RowCompletionCallback callback,
                 std::optional<Rect16> tileSubRegion = std::nullopt)
      : tileCache_(tileCache), currentTileY_(0), lastClearedTileY_(0), rowCallback_(callback)
  {
    // Calculate image dimensions from bounds
    uint32_t imageWidth = imageBounds.x1 - imageBounds.x0;
    uint32_t imageHeight = imageBounds.y1 - imageBounds.y0;

    if(imageWidth == 0 || imageHeight == 0 || tileWidth == 0 || tileHeight == 0)
      throw std::invalid_argument("Dimensions must be positive");

    numTileCols_ = static_cast<uint16_t>(((uint64_t)imageWidth + tileWidth - 1) / tileWidth);
    numTileRows_ = static_cast<uint16_t>(((uint64_t)imageHeight + tileHeight - 1) / tileHeight);

    // Default to full region if subregion is not provided
    if(tileSubRegion.has_value())
    {
      tileX0_ = tileSubRegion->x0;
      tileX1_ = tileSubRegion->x1; // Exclusive upper bound
      tileY0_ = tileSubRegion->y0;
      tileY1_ = tileSubRegion->y1; // Exclusive upper bound
    }
    else
    {
      tileX0_ = 0;
      tileX1_ = numTileCols_; // Exclusive upper bound
      tileY0_ = 0;
      tileY1_ = numTileRows_; // Exclusive upper bound
    }

    // Validate subregion
    if(tileX0_ > tileX1_ || tileY0_ > tileY1_ || tileX1_ > numTileCols_ || tileY1_ > numTileRows_)
      throw std::invalid_argument("Invalid tile range");

    tileWidth_ = tileWidth;
    tileHeight_ = tileHeight;
    imageBounds_ = imageBounds;

    // Calculate dimensions of subregion (number of tiles in half-open range)
    subregionWidth_ = static_cast<uint16_t>(tileX1_ - tileX0_);
    subregionHeight_ = static_cast<uint16_t>(tileY1_ - tileY0_);

    // Initialize completion tracking
    completedTiles_.resize(subregionWidth_ * subregionHeight_, false);
    completedTilesPerRow_.resize(subregionHeight_, 0);

    grklog.debug("Image bounds: x0=%u, y0=%u, x1=%u, y1=%u, tileWidth=%u, tileHeight=%u",
                 imageBounds_.x0, imageBounds_.y0, imageBounds_.x1, imageBounds_.y1, tileWidth_,
                 tileHeight_);
  }

  void complete(uint16_t tileIndex)
  {
    uint16_t tileX = tileIndex % numTileCols_;
    uint16_t tileY = tileIndex / numTileCols_;

    // Ignore tiles outside the subregion (half-open: x0 <= x < x1, y0 <= y < y1)
    if(tileX < tileX0_ || tileX >= tileX1_ || tileY < tileY0_ || tileY >= tileY1_)
      return;

    // Convert global tile index to local index in subregion (row-major order)
    uint16_t localX = static_cast<uint16_t>(tileX - tileX0_);
    uint16_t localY = static_cast<uint16_t>(tileY - tileY0_);
    uint16_t localIndex = static_cast<uint16_t>(localY * subregionWidth_ + localX);

    std::lock_guard<std::mutex> lock(mutex_);
    if(!completedTiles_[localIndex])
    {
      completedTiles_[localIndex] = true;
      completedTilesPerRow_[localY]++; // Increment completed tile count for this row
      grklog.debug("Tile %d (local %d, tileX=%u, tileY=%u) completed", tileIndex, localIndex, tileX,
                   tileY);

      // Check if the entire row is completed
      if((completedTilesPerRow_[localY] == subregionWidth_) && rowCallback_)
      {
        // Calculate global tile indices for the row (inclusive range)
        uint16_t tileIndexBegin = (uint16_t)(tileY * numTileCols_ + tileX0_);
        uint16_t tileIndexEnd = (uint16_t)(tileY * numTileCols_ + tileX1_);
        grklog.debug("Row %u completed, indices %u up to %u", tileY, tileIndexBegin, tileIndexEnd);
        rowCallback_(tileIndexBegin, tileIndexEnd);
      }

      auto popped = heap_.push_and_pop(localIndex);
      if(popped.has_value())
      {
        localContiguousEnd_ = static_cast<int32_t>(*popped);
        if(localContiguousEnd_ >= localWaitEnd_)
        {
          completionCV_.notify_one();
          grklog.debug("Completed tiles until index %d", localContiguousEnd_);
        }
      }
    }
    else
    {
      grklog.debug("Tile %d (local %d, tileX=%u, tileY=%u) already completed", tileIndex,
                   localIndex, tileX, tileY);
    }
  }

  bool wait(grk_wait_swath* swath)
  {
    uint32_t x0 = swath->x0;
    uint32_t y0 = swath->y0;
    uint32_t x1 = swath->x1;
    uint32_t y1 = swath->y1;

    grklog.debug("Swath canvas: x0=%u, y0=%u, x1=%u, y1=%u", x0, y0, x1, y1);

    // Validate swath bounds against image bounds
    if(x0 >= x1 || y0 >= y1 || x1 > imageBounds_.x1 || y1 > imageBounds_.y1)
    {
      swath->tile_x0 = 0;
      swath->tile_y0 = 0;
      swath->tile_x1 = 0;
      swath->tile_y1 = 0;
      swath->num_tile_cols = 0;
      grklog.debug("Invalid swath bounds: x0=%u, y0=%u, x1=%u, y1=%u", x0, y0, x1, y1);
      return false;
    }

    // Convert pixel coordinates to tile coordinates, accounting for image offset
    uint32_t x0Rel = x0 - imageBounds_.x0;
    uint32_t y0Rel = y0 - imageBounds_.y0;
    uint32_t x1Rel = x1 - imageBounds_.x0;
    uint32_t y1Rel = y1 - imageBounds_.y0;

    uint32_t x0Div = x0Rel / tileWidth_;
    uint32_t y0Div = y0Rel / tileHeight_;
    uint32_t x1Div = (x1Rel - 1) / tileWidth_;
    uint32_t y1Div = (y1Rel - 1) / tileHeight_;

    // Check for overflow before casting to uint16_t
    if(x0Div > UINT16_MAX || y0Div > UINT16_MAX || x1Div > UINT16_MAX || y1Div > UINT16_MAX)
    {
      swath->tile_x0 = 0;
      swath->tile_y0 = 0;
      swath->tile_x1 = 0;
      swath->tile_y1 = 0;
      swath->num_tile_cols = 0;
      grklog.debug("Tile coordinate overflow: x0Div=%u, y0Div=%u, x1Div=%u, y1Div=%u", x0Div, y0Div,
                   x1Div, y1Div);
      return false;
    }

    // Compute tile coordinates, constrained to subregion
    uint16_t tileX0 = std::max(tileX0_, static_cast<uint16_t>(x0Div));
    uint16_t tileY0 = std::max(tileY0_, static_cast<uint16_t>(y0Div));
    uint16_t tileX1 = std::min(tileX1_, static_cast<uint16_t>(x1Div + 1)); // Exclusive upper bound
    uint16_t tileY1 = std::min(tileY1_, static_cast<uint16_t>(y1Div + 1)); // Exclusive upper bound

    grklog.debug("Computed tile coords: x0Div=%u, y0Div=%u, x1Div=%u, y1Div=%u", x0Div, y0Div,
                 x1Div, y1Div);
    grklog.debug("Constrained tile coords: tileX0=%u, tileY0=%u, tileX1=%u, tileY1=%u", tileX0,
                 tileY0, tileX1, tileY1);

    // Populate swath with tile coordinates and grid info
    swath->tile_x0 = tileX0;
    swath->tile_y0 = tileY0;
    swath->tile_x1 = tileX1;
    swath->tile_y1 = tileY1;
    swath->num_tile_cols = numTileCols_;

    // Check if tile row has advanced for clearing previous rows
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if(tileY0 > currentTileY_ && tileY0 > lastClearedTileY_)
      {
        // Clear all completed tiles in previous tile rows
        for(uint16_t clearTileY = lastClearedTileY_ + 1; clearTileY < tileY0; ++clearTileY)
        {
          for(uint16_t tileX = tileX0_; tileX < tileX1_; ++tileX)
          {
            uint32_t tileIndexCalc = static_cast<uint32_t>(clearTileY) * numTileCols_ + tileX;
            if(tileIndexCalc > UINT16_MAX)
              continue; // Skip if tile index overflows
            uint16_t tileIndex = static_cast<uint16_t>(tileIndexCalc);
            uint16_t localX = tileX - tileX0_;
            uint16_t localY = clearTileY - tileY0_;
            if(localX < subregionWidth_ && localY < subregionHeight_)
            {
              uint16_t localIndex = static_cast<uint16_t>(localY * subregionWidth_ + localX);
              if(completedTiles_[localIndex])
              {
                grklog.debug(
                    "Clearing ITileProcessor at tile index %d (local %d, tileX=%u, tileY=%u)",
                    tileIndex, localIndex, tileX, clearTileY);
                tileCache_->release(tileIndex);
              }
            }
          }
        }
        lastClearedTileY_ = tileY0 - 1;
        grklog.debug("Cleared tile rows up to tileY=%u", lastClearedTileY_);
      }
      currentTileY_ = tileY0;
      grklog.debug("Tile row transition: currentTileY=%u, lastClearedTileY=%u", currentTileY_,
                   lastClearedTileY_);
    }

    // Compute all local indices in the swath
    std::vector<uint16_t> swathIndices;
    for(uint16_t tileY = tileY0; tileY < tileY1; ++tileY)
    {
      for(uint16_t tileX = tileX0; tileX < tileX1; ++tileX)
      {
        uint16_t localX = tileX - tileX0_;
        uint16_t localY = tileY - tileY0_;
        uint16_t localIndex = static_cast<uint16_t>(localY * subregionWidth_ + localX);
        swathIndices.push_back(localIndex);
      }
    }
    uint16_t localEnd = swathIndices.empty() ? 0 : swathIndices.back();

    // Wait until all tiles in the swath are completed
    {
      std::unique_lock<std::mutex> lock(mutex_);
      auto allCompleted = [&]() {
        for(uint16_t idx : swathIndices)
        {
          if(!completedTiles_[idx])
            return false;
        }
        return true;
      };
      if(!allCompleted())
      {
        grklog.debug(
            "Waiting for swath ending at tile %d (local %d), tiles: x0=%d, y0=%d, x1=%d, y1=%d",
            (tileY1 - 1) * numTileCols_ + (tileX1 - 1), localEnd, tileX0, tileY0, tileX1, tileY1);
        if(localWaitEnd_ < localEnd)
          localWaitEnd_ = localEnd;
        completionCV_.wait(lock, allCompleted);
        grklog.debug("End wait with contiguous end %d", localContiguousEnd_);
      }
      else
      {
        grklog.debug(
            "No waiting for swath ending at tile %d (local %d), tiles: x0=%d, y0=%d, x1=%d, y1=%d",
            (tileY1 - 1) * numTileCols_ + (tileX1 - 1), localEnd, tileX0, tileY0, tileX1, tileY1);
      }
    }
    bool finalWait =
        localContiguousEnd_ == static_cast<int32_t>(subregionWidth_ * subregionHeight_ - 1);

    grklog.debug("Swath completed: tileX0=%d, tileY0=%d, tileX1=%d, tileY1=%d", swath->tile_x0,
                 swath->tile_y0, swath->tile_x1, swath->tile_y1);
    return finalWait;
  }

private:
  TileCache* tileCache_ = nullptr; // Stored for row-based clearing
  SimpleHeap<uint16_t> heap_;
  std::vector<bool> completedTiles_; // Tracks completion status of each tile in subregion
  std::vector<uint16_t> completedTilesPerRow_; // Tracks number of completed tiles per row
  int32_t localWaitEnd_ = -1;
  int32_t localContiguousEnd_ = -1;
  std::mutex mutex_;
  std::condition_variable completionCV_;
  uint16_t numTileCols_, numTileRows_;
  uint32_t tileWidth_, tileHeight_;
  Rect32 imageBounds_;
  uint16_t tileX0_, tileX1_, tileY0_, tileY1_;
  uint16_t subregionWidth_, subregionHeight_;
  uint16_t currentTileY_; // Tracks the current swath's starting tile row
  uint16_t lastClearedTileY_; // Tracks the last tile row cleared
  RowCompletionCallback rowCallback_; // Callback for row completion
};

} // namespace grk