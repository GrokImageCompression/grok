/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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

ResFlow::ResFlow(void)
	: blocks_(new FlowComponent()), waveletHoriz_(new FlowComponent()),
	  waveletVert_(new FlowComponent())
{}
void ResFlow::graph(void)
{
	blocks_->precede(waveletHoriz_);
	waveletHoriz_->precede(waveletVert_);
}
ResFlow* ResFlow::precede(ResFlow* successor)
{
	waveletVert_->precede(successor->blocks_);

	return this;
}
ResFlow* ResFlow::precede(FlowComponent* successor)
{
	waveletVert_->precede(successor);

	return this;
}
ResFlow::~ResFlow(void)
{
	delete blocks_;
	delete waveletHoriz_;
	delete waveletVert_;
}
ImageComponentFlow::ImageComponentFlow(uint8_t numResolutions)
	: numResFlows_(numResolutions), resFlows_(nullptr), waveletFinalCopy_(nullptr)
{
	if(numResFlows_)
	{
		// lowest two resolutions are grouped together
		if(numResFlows_ > 1)
			numResFlows_--;
		resFlows_ = new ResFlow[numResFlows_];
	}
}
ImageComponentFlow::~ImageComponentFlow()
{
	delete[] resFlows_;
	delete waveletFinalCopy_;
}
void ImageComponentFlow::graph(void)
{
	for(uint8_t i = 0; i < numResFlows_; ++i)
		(resFlows_ + i)->graph();
	for(uint8_t i = 0; 0 < numResFlows_ - 1; ++i)
		(resFlows_ + i)->precede(resFlows_ + i + 1);
	if(waveletFinalCopy_)
		(resFlows_ + numResFlows_ - 1)->precede(waveletFinalCopy_);
}
ResFlow* ImageComponentFlow::getResFlow(uint8_t resFlowNo)
{
	return (resFlows_ && resFlowNo < numResFlows_) ? resFlows_ + resFlowNo : nullptr;
}
std::string ImageComponentFlow::genBlockFlowTaskName(uint8_t resFlowNo)
{
	std::stringstream ss;
	ss << "blockFlowTask-" << resFlowNo;

	return ss.str();
}
