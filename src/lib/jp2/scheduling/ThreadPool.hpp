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

#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wimplicit-int-float-conversion"
#endif
#endif
#include <taskflow/taskflow.hpp>
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif

class ExecSingleton
{
  public:
	static tf::Executor* instance(uint32_t numthreads)
	{
		static tf::Executor singleton(numthreads ? numthreads
												 : std::thread::hardware_concurrency());

		return &singleton;
	}
	static tf::Executor* get()
	{
		return instance(0);
	}
	static void release()
	{
		get()->shutdown();
	}
	static uint32_t threadId(void){
		return get()->num_workers() > 1 ? (uint32_t)ExecSingleton::get()->this_worker_id() : 0;
	}
};
