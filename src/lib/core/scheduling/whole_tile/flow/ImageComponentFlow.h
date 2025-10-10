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
 */

#pragma once

#include "FlowComponent.h"

namespace grk
{
/**
 * @struct Resflow
 * @brief Stores @ref FlowComponent for packets, blocks, horizontal wavelet
 * and vertical wavelet, for a given resolution
 *
 */
struct Resflow
{
  /**
   * @brief Construct a new Resflow object
   *
   */
  Resflow(void);
  /**
   * @brief Destroy the Resflow object
   *
   */
  ~Resflow(void);

  /**
   * @brief Gets the Packets @ref FlowComponent
   *
   * @return FlowComponent* pointing to this
   */
  FlowComponent* getPacketsFlow(void);
  /**
   * @brief Disables wavelet
   *
   */
  void disableWavelet(void);
  /**
   * @brief creates FlowComponent DAG
   *
   */
  void graph(void);
  /**
   * @brief Adds all @ref FlowComponents to another @ref tf::Taskflow
   *
   * @param composition composition @ref tf::Taskflow
   * @return Resflow* pointing to this
   */
  Resflow* addTo(tf::Taskflow& composition);

  /**
   * @brief Schedules this Resflow to precede another Resflow
   *
   * @param successor Resflow to succeed this Resflow
   * @return Resflow* pointing to this
   */
  Resflow* precede(Resflow* successor);
  /**
   * @brief Schedules this Resflow to precede another @ref FlowComponent
   *
   * @param successor @ref FlowComponent scheduled after this Resflow
   * @return Resflow* pointing to this
   */
  Resflow* precede(FlowComponent* successor);
  /**
   * @brief Gets final @ref FlowComponent in T1 stage
   *
   * @return FlowComponent*
   */
  FlowComponent* getFinalFlowT1(void);

  /**
   * @brief blocks @ref FlowComponent
   *
   */
  FlowComponent* blocks_;
  /**
   * @brief Horizontal wavelet @ref FlowComponent
   *
   */
  FlowComponent* waveletHoriz_;
  /**
   * @brief Vertical wavelet @ref FlowComponent
   *
   */
  FlowComponent* waveletVert_;
  /**
   * @brief if true, perform wavelet, otherwise do not perform wavelet
   *
   */
  bool doWavelet_;
};

/**
 * @class ImageComponentFlow
 * @brief Image component flow storing array of @ref Resflow
 *
 */
class ImageComponentFlow
{
public:
  /**
   * @brief Construct a new ImageComponentFlow object
   *
   * @param numresolutions number of resolutions for this component
   */
  ImageComponentFlow(uint8_t numresolutions);
  /**
   * @brief Destroy the ImageComponentFlow object
   *
   */
  virtual ~ImageComponentFlow(void);
  /**
   * @brief Enables region decompression
   *
   */
  void setRegionDecompression(void);
  /**
   * @brief Generates block flow task name
   *
   * @param resFlowNo resolution flow number
   * @return std::string name of block flow task
   */
  std::string genBlockFlowTaskName(uint8_t resFlowNo);
  /**
   * @brief Gets the Resflow object
   *
   * @param resFlowNo resolution flow number
   * @return Resflow*
   */
  Resflow* getResflow(uint8_t resFlowNo);
  /**
   * @brief Creates DAG for this flow
   *
   */
  void graph(void);
  /**
   * @brief Adds this flow to another @ref tf::Taskflow as a composition
   *
   * @param composition composition @ref tf::Taskflow
   * @return ImageComponentFlow*
   */
  ImageComponentFlow* addTo(tf::Taskflow& composition);
  /**
   * @brief Gets final flow
   *
   * @return FlowComponent*
   */
  FlowComponent* getFinalFlowT1(void);
  /**
   * @brief Gets pre or post processing flow
   *
   * @param codecFlow
   * @return FlowComponent*
   */
  FlowComponent* getPrePostProc(tf::Taskflow& codecFlow);

  /**
   * @brief number of @ref Resflow objects
   *
   */
  uint8_t numResflows_;
  /**
   * @brief array of @ref Resflow objects
   *
   */
  Resflow* resFlows_;
  /**
   * @brief @ref FlowComponent for final wavelet copy into output buffer
   *
   */
  FlowComponent* waveletFinalCopy_;
  /**
   * @brief @ref FlowComponent for pre or post processing
   *
   */
  FlowComponent* prePostProc_;
};

} // namespace grk
