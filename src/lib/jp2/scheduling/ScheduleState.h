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

#pragma once

class ScheduleState {
public:
	ScheduleState(uint8_t numResolutions);
	virtual ~ScheduleState();
	std::string genResBlockTaskName(uint8_t resno);

	uint8_t numResolutions_;

	// create one tf::Taskflow for all blocks in a given resolution, and create one single
	// tf::Taskflow object codecFlow_, composed of all resolution block flows
	tf::Task **blockTasks_;
	tf::Task *resBlockTasks_;
	tf::Taskflow *resBlockFlows_;
	tf::Taskflow codecFlow_;
};
