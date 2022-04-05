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

Composee::Composee(void) : tasks_(nullptr){
}
Composee::~Composee(void){
	delete[] tasks_;
}
Composee* Composee::alloc(uint64_t numTasks){
	if (tasks_)
		delete[] tasks_;
	tasks_ = new tf::Task[numTasks];
	for(uint64_t i = 0; i < numTasks; i++)
		tasks_[i] = flow_.placeholder();

	return this;
}
Composee* Composee::composed_by(tf::Taskflow &composer, std::string name){
	composedFlowTask_ = composer.composed_of(flow_).name(name);

	return this;
}

ResFlow::ResFlow(void) : blockFlow_(new Composee()),
						   waveletFlow_(new Composee()) {
}
ResFlow::~ResFlow(void){
	delete blockFlow_;
	delete waveletFlow_;
}

ComponentFlow::ComponentFlow(uint8_t numResolutions) : numResFlows_(numResolutions),
														resFlows_(nullptr)
{
	if (numResFlows_){
		// lowest two resolutions are grouped together
		if (numResFlows_ > 1)
			numResFlows_--;
		resFlows_ = new ResFlow[numResFlows_];
	}
}
ComponentFlow::~ComponentFlow() {
	delete[] resFlows_;
}
std::string ComponentFlow::genBlockFlowTaskName(uint8_t resno){
	std::stringstream ss;
	ss << "blockFlowTask-" << resno;

	return ss.str();
}

