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

std::unique_ptr<tf::Executor> ExecSingleton::instance_ = nullptr;
std::mutex ExecSingleton::mutex_;

namespace grk
{

Scheduler::Scheduler(Tile* tile)
	: success(true), tile_(tile), numcomps_(tile->numcomps_), prePostProc_(nullptr)
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
   for(const auto& t : t1Implementations)
	  delete t;
   delete prePostProc_;
}
bool Scheduler::run(void)
{
   ExecSingleton::get().run(codecFlow_).wait();

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

FlowComponent* Scheduler::getPrePostProc(void)
{
   if(!prePostProc_)
   {
	  prePostProc_ = new FlowComponent();
	  prePostProc_->addTo(codecFlow_);
   }

   return prePostProc_;
}

} // namespace grk
