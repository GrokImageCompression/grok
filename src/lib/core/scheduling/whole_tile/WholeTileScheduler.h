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
 * @class WholeTileScheduler
 * @brief abstract class to graph and execute T1 tasks for whole tile
 *
 * Task scheduling will be performed by derived classes
 */
class WholeTileScheduler : public CodecScheduler
{
public:
  /**
   * @brief Constructs a WholeTileScheduler
   * @param numComps number of components
   */
  WholeTileScheduler(uint16_t numComps);

  /**
   * @brief Destroys a WholeTileScheduler
   */
  virtual ~WholeTileScheduler();

  /**
   * @brief Releases flow components
   */
  virtual void release(void) override;

  /**
   * @brief Gets @ref ImageComponentFlow for component
   * @brief compno component number
   */
  ImageComponentFlow* getImageComponentFlow(uint16_t compno);

protected:
  /**
   * @brief Calculates task graph for component
   * @param compno component number
   */
  void graph(uint16_t compno);

  /**
   * @brief store image component flows
   */
  std::vector<ImageComponentFlow*> imageComponentFlow_;
};

} // namespace grk
