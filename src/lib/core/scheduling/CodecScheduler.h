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

#include "grk_taskflow.h"

namespace grk
{

class ResolutionChecker
{
public:
  ResolutionChecker(uint16_t numComponents, TileProcessor* tileProcessor, bool cacheAll);

  // Check if a specific component contains a given resolution
  bool contains(uint16_t compno, uint8_t resolution) const;

  std::pair<uint8_t, uint8_t> getResBounds(uint16_t compno);

private:
  // Pair of <resBegin, resUpperBound> for each component
  std::vector<std::pair<uint8_t, uint8_t>> componentResolutions_;
};

/**
 * @struct DifferentialInfo
 * @brief Stores number of layers compressed in differential decompression
 *
 */
struct DifferentialInfo
{
  /**
   * @brief Construct a new Differential Info object
   *
   */
  DifferentialInfo(void) : layersDecompressed_(0) {}

  /**
   * @brief number of layers decompressed
   *
   */
  uint16_t layersDecompressed_;
};

/**
 * @class CodecScheduler
 * @brief An abstract class that can execute T1 phase of codec
 * by running a tf::Executor
 *
 * This class is derived from @ref FlowComponent, and acts as the root
 * tf::Taskflow. Scheduling of tasks for this root is implemented
 *  in derived classes.
 */
class CodecScheduler : public FlowComponent
{
public:
  /**
   * @brief Contructs a CodecScheduler
   * @param numComps number of image components
   */
  explicit CodecScheduler(uint16_t numComps);

  /**
   * @brief Destroys a CodecScheduler
   */
  virtual ~CodecScheduler();

  /**
   * @brief Schedules all T1 tasks for a @ref TileProcessor
   *
   * @param proc @ref TileProcessor
   * @return true if successful
   */
  virtual bool schedule(TileProcessor* proc) = 0;

  /**
   * @brief Runs tf::Executor
   */
  void run(void);

  /**
   * @brief Waits for tf::Executor to complete
   *
   * @return true if all tasks succeeded
   */
  bool wait(void);

  /**
   * @brief Releases Taskflow resources
   *
   */
  virtual void release(void) = 0;

protected:
  void releaseCoders(void);

  /**
   * @brief atomic tracking of compress/decompress success
   */
  std::atomic_bool success;

  /**
   * @brief number of components
   */
  uint16_t numcomps_;

  /**
   * @brief pool of @ref ICoder
   *
   */
  std::vector<t1::ICoder*> coders_;

  /**
   * @brief @ref tf::Future<void> resulting from running scheduler
   *
   */
  tf::Future<void> runFuture_;
};

} // namespace grk
