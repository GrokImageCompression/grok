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
	Scheduler(uint8_t numResolutions);
	virtual ~Scheduler();
	virtual bool schedule(void) = 0;
	bool run(void);

	ScheduleState* getState(void);
  protected:
	std::atomic_bool success;
	std::vector<T1Interface*> t1Implementations;
	ScheduleState *scheduleState_;
};

} // namespace grk
