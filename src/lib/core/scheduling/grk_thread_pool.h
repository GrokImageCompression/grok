/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

#include "grok.h"
#include "grk_taskflow.h"

namespace grk
{

/**
 * @brief Returns a reference to the core library's tf::Executor.
 *
 * The executor is created by grk_initialize() and destroyed by
 * grk_deinitialize().  Calling this outside that window is undefined.
 */
inline tf::Executor& executor()
{
  return *static_cast<tf::Executor*>(grk_thread_pool());
}

/**
 * @brief Returns the total number of worker threads in the core pool.
 */
inline size_t num_workers()
{
  return grk_num_workers();
}

/**
 * @brief Returns the TaskFlow worker id of the calling thread.
 *
 * @return worker id if called from inside a TaskFlow task, 0 otherwise
 */
inline uint32_t worker_id()
{
  return grk_worker_id();
}

} // namespace grk
