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


ScheduleState::ScheduleState(uint8_t numResolutions) : numResFlows_(numResolutions),
														blockTasks_(nullptr),
														resBlockTasks_(nullptr),
														resBlockFlows_(nullptr)
{
	codecFlow_.name("codecFlow");
	if (numResFlows_){
		// lowest two resolutions are grouped together
		if (numResFlows_ > 1)
			numResFlows_--;
		blockTasks_ = new tf::Task*[numResFlows_];
		for (uint8_t i = 0; i < numResFlows_; ++i)
			blockTasks_[i] = nullptr;
		resBlockTasks_ = new tf::Task[numResFlows_];
		resBlockFlows_ = new tf::Taskflow[numResFlows_];
	}
}
ScheduleState::~ScheduleState() {
	if (blockTasks_){
		for(uint8_t i = 0; i < numResFlows_; ++i)
			delete[] blockTasks_[i];
		delete[] blockTasks_;
	}
	delete[] resBlockTasks_;
	delete[] resBlockFlows_;
}
std::string ScheduleState::genResBlockTaskName(uint8_t resno){
	std::stringstream ss;
	ss << "resBlockTask-" << resno;

	return ss.str();
}

