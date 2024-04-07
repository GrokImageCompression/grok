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
#include "grk_includes.h"

namespace grk
{
ResFlow::ResFlow(void)
	: packets_(nullptr), blocks_(new FlowComponent()), waveletHoriz_(new FlowComponent()),
	  waveletVert_(new FlowComponent()), doWavelet_(true)
{}
FlowComponent* ResFlow::getPacketsFlow(void)
{
   if(!packets_)
   {
	  packets_ = new FlowComponent();
	  packets_->precede(blocks_);
   }

   return packets_;
}
void ResFlow::disableWavelet(void)
{
   doWavelet_ = false;
}
void ResFlow::graph(void)
{
   if(doWavelet_)
   {
	  blocks_->precede(waveletHoriz_);
	  waveletHoriz_->precede(waveletVert_);
   }
}
ResFlow* ResFlow::addTo(tf::Taskflow& composition)
{
   if(packets_)
	  packets_->addTo(composition);
   assert(blocks_);
   blocks_->addTo(composition);
   if(doWavelet_)
   {
	  waveletHoriz_->addTo(composition);
	  waveletVert_->addTo(composition);
   }

   return this;
}
ResFlow* ResFlow::precede(ResFlow* successor)
{
   assert(successor);
   if(doWavelet_)
	  waveletVert_->precede(successor->blocks_);

   return this;
}
ResFlow* ResFlow::precede(FlowComponent* successor)
{
   assert(successor);
   if(doWavelet_)
	  waveletVert_->precede(successor);
   else
	  blocks_->precede(successor);

   return this;
}
FlowComponent* ResFlow::getFinalFlowT1(void)
{
   return doWavelet_ ? waveletVert_ : blocks_;
}
ResFlow::~ResFlow(void)
{
   delete packets_;
   delete blocks_;
   delete waveletHoriz_;
   delete waveletVert_;
}
ImageComponentFlow::ImageComponentFlow(uint8_t numResolutions)
	: numResFlows_(numResolutions), resFlows_(nullptr), waveletFinalCopy_(nullptr),
	  prePostProc_(nullptr)
{
   if(numResFlows_)
   {
	  bool noWavelet = numResFlows_ == 1;

	  // lowest two resolutions are grouped together
	  if(numResFlows_ > 1)
		 numResFlows_--;
	  resFlows_ = new ResFlow[numResFlows_];
	  if(noWavelet)
		 resFlows_[0].disableWavelet();
   }
}
ImageComponentFlow::~ImageComponentFlow()
{
   delete[] resFlows_;
   delete waveletFinalCopy_;
   delete prePostProc_;
}
void ImageComponentFlow::setRegionDecompression(void)
{
   waveletFinalCopy_ = new FlowComponent();
}
void ImageComponentFlow::graph(void)
{
   for(uint8_t i = 0; i < numResFlows_; ++i)
	  (resFlows_ + i)->graph();
   for(uint8_t i = 0; i < numResFlows_ - 1; ++i)
	  (resFlows_ + i)->precede(resFlows_ + i + 1);
   if(waveletFinalCopy_)
	  (resFlows_ + numResFlows_ - 1)->precede(waveletFinalCopy_);
}
FlowComponent* ImageComponentFlow::getFinalFlowT1(void)
{
   return waveletFinalCopy_ ? waveletFinalCopy_ : (resFlows_ + numResFlows_ - 1)->getFinalFlowT1();
}
ImageComponentFlow* ImageComponentFlow::addTo(tf::Taskflow& composition)
{
   for(uint8_t i = 0; i < numResFlows_; ++i)
	  (resFlows_ + i)->addTo(composition);
   if(waveletFinalCopy_)
	  waveletFinalCopy_->addTo(composition);

   return this;
}
ResFlow* ImageComponentFlow::getResFlow(uint8_t resFlowNo)
{
   return (resFlows_ && resFlowNo < numResFlows_) ? resFlows_ + resFlowNo : nullptr;
}
FlowComponent* ImageComponentFlow::getPrePostProc(tf::Taskflow& codecFlow)
{
   if(!prePostProc_)
   {
	  prePostProc_ = new FlowComponent();
	  prePostProc_->addTo(codecFlow);
   }

   return prePostProc_;
}
std::string ImageComponentFlow::genBlockFlowTaskName(uint8_t resFlowNo)
{
   std::stringstream ss;
   ss << "blockFlowTask-" << resFlowNo;

   return ss.str();
}

} // namespace grk
