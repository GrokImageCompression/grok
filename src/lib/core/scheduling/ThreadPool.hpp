/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
#pragma GCC diagnostic ignored "-Wconversion"
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
   // Deleted copy constructor and assignment operator
   ExecSingleton(const ExecSingleton&) = delete;
   ExecSingleton& operator=(const ExecSingleton&) = delete;

   // Get instance of the Singleton with a specific number of threads
   static tf::Executor& instance(uint32_t numthreads)
   {
	  std::lock_guard<std::mutex> lock(mutex_);
	  instance_ = std::make_unique<tf::Executor>(numthreads ? numthreads
															: std::thread::hardware_concurrency());

	  return *instance_;
   }

   // Get current instance of the Singleton (create with hardware concurrency if null)
   static tf::Executor& get(void)
   {
	  std::lock_guard<std::mutex> lock(mutex_);
	  if(!instance_)
		 return instance(0);
	  return *instance_;
   }

   // Destroy the Singleton instance
   static void destroy()
   {
	  std::lock_guard<std::mutex> lock(mutex_);
	  instance_.reset();
   }

   static uint32_t threadId(void)
   {
	  return get().num_workers() > 1 ? (uint32_t)get().this_worker_id() : 0;
   }

 private:
   ExecSingleton() = default;

   static std::unique_ptr<tf::Executor> instance_;
   static std::mutex mutex_;
};
