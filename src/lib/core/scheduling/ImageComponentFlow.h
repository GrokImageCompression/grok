/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
struct ResFlow
{
   ResFlow(void);
   ~ResFlow(void);

   FlowComponent* getPacketsFlow(void);
   void disableWavelet(void);
   void graph(void);
   ResFlow* addTo(tf::Taskflow& composition);
   ResFlow* precede(ResFlow* successor);
   ResFlow* precede(FlowComponent* successor);
   FlowComponent* getFinalFlowT1(void);
   FlowComponent* packets_;
   FlowComponent* blocks_;
   FlowComponent* waveletHoriz_;
   FlowComponent* waveletVert_;
   bool doWavelet_;
};

class ImageComponentFlow
{
 public:
   ImageComponentFlow(uint8_t num_resolutions);
   virtual ~ImageComponentFlow(void);
   void setRegionDecompression(void);
   std::string genBlockFlowTaskName(uint8_t resFlowNo);
   ResFlow* getResFlow(uint8_t resFlowNo);
   void graph(void);
   ImageComponentFlow* addTo(tf::Taskflow& composition);
   FlowComponent* getFinalFlowT1(void);
   FlowComponent* getPrePostProc(tf::Taskflow& codecFlow);

   uint8_t numResFlows_;
   ResFlow* resFlows_;
   FlowComponent* waveletFinalCopy_;
   FlowComponent* prePostProc_;
};

} // namespace grk
