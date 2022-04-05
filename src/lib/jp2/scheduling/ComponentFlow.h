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


struct Composee{
	Composee(void);
	~Composee(void);

	Composee* alloc(uint64_t numTasks);
	Composee* composed_by(tf::Taskflow &composer);
	Composee* name(const std::string& name);

	tf::Task *tasks_;
	tf::Taskflow flow_;
	tf::Task composedFlowTask_;
};

struct ResFlow{
	ResFlow(void);
	~ResFlow(void);

	Composee *blockFlow_;
	Composee *waveletHorizLFlow_;
	Composee *waveletHorizHFlow_;
	Composee *waveletVertFlow_;
};

class ComponentFlow {
public:
	ComponentFlow(uint8_t numResolutions);
	virtual ~ComponentFlow();
	std::string genBlockFlowTaskName(uint8_t resFlowNo);
	ResFlow *getResFlow(uint8_t resFlowNo);

	uint8_t numResFlows_;

	// create one tf::Taskflow for all blocks in a given resolution, and create one single
	// tf::Taskflow object codecFlow_, composed of all resolution block flows
	ResFlow *resFlows_;
	Composee *waveletFinalCopyFlow_;
};
