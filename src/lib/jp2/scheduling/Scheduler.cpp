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
Scheduler::Scheduler(Tile* tile) : success(true), tile_(tile), numcomps_(tile->numcomps_)
{
	imageComponentFlows_ = new ImageComponentFlow*[numcomps_];
	for(uint16_t compno = 0; compno < numcomps_; ++compno)
		imageComponentFlows_[compno] = nullptr;
}
Scheduler::~Scheduler()
{
	for(uint16_t compno = 0; compno < numcomps_; ++compno)
		delete imageComponentFlows_[compno];
	delete[] imageComponentFlows_;
	for(auto& t : t1Implementations)
		delete t;
}
bool Scheduler::run(void)
{
	ExecSingleton::get()->run(codecFlow_).wait();

	return success;
}
void Scheduler::graph(uint16_t compno)
{
	assert(compno < numcomps_);
	imageComponentFlows_[compno]->graph();
}
ImageComponentFlow* Scheduler::getImageComponentFlow(uint16_t compno)
{
	return (imageComponentFlows_ && compno < numcomps_) ? imageComponentFlows_[compno] : nullptr;
}
tf::Taskflow& Scheduler::getCodecFlow(void)
{
	return codecFlow_;
}

} // namespace grk
