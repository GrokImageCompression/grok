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

namespace grk
{
class Scheduler
{
  public:
	Scheduler(Tile* tile);
	virtual ~Scheduler();
	virtual bool schedule(uint16_t compno) = 0;
	bool run(void);
	ComponentFlow* getComponentFlow(uint16_t compno);
	tf::Taskflow& getCodecFlow(void);
  protected:
	std::atomic_bool success;
	std::vector<T1Interface*> t1Implementations;
	ComponentFlow **componentFlows_;
	tf::Taskflow codecFlow_;
	Tile* tile_;
	uint16_t numcomps_;
};

} // namespace grk
