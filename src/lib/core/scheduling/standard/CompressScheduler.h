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

#include "SchedulerStandard.h"
#include "RateControlStats.h"
#include "ProgressiveSlopeEstimator.h"
#include <memory>

namespace grk
{
class CompressScheduler : public SchedulerStandard
{
public:
  /**
   * @brief Constructs a CompressScheduler
   * @param tile @ref Tile
   * @param needsRateControl true if rate control needed
   * @param tcp @ref TileCodingParams
   * @param mct_norms array of mct norms
   * @param mct_numcomps number of mct components
   */
  CompressScheduler(Tile* tile, bool needsRateControl, TileCodingParams* tcp,
                    const double* mct_norms, uint16_t mct_numcomps,
                    bool progressiveRateControl = false);
  /**
   * @brief Destroys a CompressScheduler
   */
  ~CompressScheduler() override = default;

  bool scheduleT1(ITileProcessor* proc) override;
  bool populateT1Flow(FlowComponent* flow);

  /**
   * @brief Get rate control stats collected during T1 encoding
   */
  const RateControlStats& getRateControlStats() const
  {
    return rateControlStats_;
  }

private:
  /**
   * @brief compress next block
   *
   * @param workerId worker id
   * @param maxBlocks maximum number of blocks
   * @return true if successful
   */
  bool compress(size_t workerId, uint64_t maxBlocks);

  /**
   * @brief compress block
   *
   * @param coder @ref ICoder coder implementation
   * @param block @ref CompressBlockExec compression block
   */
  void compress(t1::ICoder* coder, t1::CompressBlockExec* block);

  /**
   * @brief Initialize the progressive slope estimator from the block list.
   *
   * Computes total sample count across all blocks and target rate (bytes/sample),
   * then constructs the ProgressiveSlopeEstimator if a rate target is available.
   *
   * @param blocks Vector of all blocks to be encoded in this tile.
   */
  void initSlopeEstimator(const std::vector<t1::CompressBlockExec*>& blocks);

  /**
   * @brief @ref Tile to compress
   */
  Tile* tile_;

  /**
   * @brief mutex to serialize distortion decrease from blocks
   */
  mutable std::mutex distortion_mutex_;
  /**
   * @brief true if rate control requested
   */
  bool needsRateControl_;

  /**
   * @brief true if progressive rate control (early termination) is enabled
   */
  bool progressiveRateControl_;
  /**
   * @brief vector of @ref CompressBlockExec encode blocks
   */
  std::vector<t1::CompressBlockExec*> encodeBlocks_;

  /**
   * @brief atomic counter to keep track of number of encoded blocks
   */
  std::atomic<int64_t> blockCount_;

  /**
   * @brief @ref TileCodingParams for this tile
   */
  TileCodingParams* tcp_;

  /**
   * @brief array of mct norms
   */
  const double* mct_norms_;

  /**
   * @brief number of components to apply mct to
   */
  uint16_t mct_numcomps_;

  /**
   * @brief rate control statistics collected during T1 encoding
   */
  RateControlStats rateControlStats_;

  /**
   * @brief Progressive slope estimator for intra-frame early pass termination.
   *
   * When a target rate is known, this estimator builds a slope histogram from
   * completed blocks and publishes a conservative early-stop threshold that
   * subsequent blocks can use to skip encoding trailing bit-planes that would
   * be discarded by PCRD.
   *
   * Null when rate control is not applicable (lossless, no rate target).
   */
  std::unique_ptr<ProgressiveSlopeEstimator> slopeEstimator_;
};

} // namespace grk
