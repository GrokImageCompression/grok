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

#ifdef _WIN32

class ExecSingleton {
public:
	static tf::Executor* instance(uint32_t numthreads)
	{
		std::unique_lock<std::mutex> lock(singleton_mutex);
		if(!singleton) {
			if (numthreads)
				singleton = new tf::Executor(numthreads);
			else
				singleton = new tf::Executor();
		}

		return singleton;
	}
	static tf::Executor* get()
	{
		return instance(0);
	}
	static void release()
	{
		std::unique_lock<std::mutex> lock(singleton_mutex);
		delete singleton;
		singleton = nullptr;
	}
private:
	static tf::Executor* singleton;
	static std::mutex singleton_mutex;

};

#else

class ExecSingleton {
public:
	static tf::Executor* instance(uint32_t numthreads)
	{
		static tf::Executor* singleton = new tf::Executor(numthreads ? numthreads : std::thread::hardware_concurrency());

		return singleton;
	}
	static tf::Executor* get()
	{
		return instance(0);
	}
	static void release()
	{
		//no-op
	}
};


#endif

