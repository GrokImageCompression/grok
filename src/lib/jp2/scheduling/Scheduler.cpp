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

namespace grk
{

Scheduler::Scheduler(Tile* tile) : success(true),tile_(tile), numcomps_(tile->numcomps_){
	assert(tile);
	componentFlows_ = new ComponentFlow*[numcomps_];
	for (uint16_t compno = 0; compno < numcomps_; ++compno){
		uint8_t numResolutions = (tile->comps+compno)->highestResolutionDecompressed+1;
		componentFlows_[compno] = numResolutions ? new ComponentFlow(numResolutions) : nullptr;
	}
}
Scheduler::~Scheduler()
{
	for (uint16_t compno = 0; compno < numcomps_; ++compno)
		delete componentFlows_[compno];
	delete[] componentFlows_;
	for(auto& t : t1Implementations)
		delete t;
}
bool Scheduler::run(void) {
	ExecSingleton::get()->run(codecFlow_).wait();

	return success;
}
ComponentFlow* Scheduler::getComponentFlow(uint16_t compno){
	return (componentFlows_ && compno < numcomps_) ? componentFlows_[compno] : nullptr;
}
tf::Taskflow& Scheduler::getCodecFlow(void){
	return codecFlow_;
}

} // namespace grk
