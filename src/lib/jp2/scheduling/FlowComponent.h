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


struct FlowComponent{
	FlowComponent(void) : tasks_(nullptr){
	}
	~FlowComponent(void){
		delete[] tasks_;
	}
	FlowComponent* alloc(uint64_t numTasks){
		if (tasks_)
			delete[] tasks_;
		tasks_ = new tf::Task[numTasks];
		for(uint64_t i = 0; i < numTasks; i++)
			tasks_[i] = flow_.placeholder();

		return this;
	}
	FlowComponent* add_to(tf::Taskflow &composition){
		composedFlowTask_ = composition.composed_of(flow_);
		return this;
	}
	FlowComponent* precede(FlowComponent *successor){
		composedFlowTask_.precede(successor->composedFlowTask_);
		return this;
	}
	FlowComponent* name(const std::string& name) {
	  composedFlowTask_.name(name);
	  return this;
	}

	tf::Task *tasks_;
	tf::Taskflow flow_;
	tf::Task composedFlowTask_;
};
