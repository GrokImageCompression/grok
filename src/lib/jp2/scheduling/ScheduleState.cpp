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

ResState::ResState(void) : blockTasks_(nullptr){
}
ResState::~ResState(void){
	delete[] blockTasks_;
}

ScheduleState::ScheduleState(uint8_t numResolutions) : numResFlows_(numResolutions),
														resStates_(nullptr)
{
	codecFlow_.name("codecFlow");
	if (numResFlows_){
		// lowest two resolutions are grouped together
		if (numResFlows_ > 1)
			numResFlows_--;
		resStates_ = new ResState[numResFlows_];
	}
}
ScheduleState::~ScheduleState() {
	delete[] resStates_;
}
std::string ScheduleState::genBlockFlowTaskName(uint8_t resno){
	std::stringstream ss;
	ss << "blockFlowTask-" << resno;

	return ss.str();
}

