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

#include <cstddef>
#include <thread>

/**
 * @class FRBSingleton
 * @brief Stub — freebyrd thread pool has been removed.
 *
 * Retains the API surface so that SchedulerFreebyrd and StripDecompressor
 * continue to compile, but all methods are no-ops.
 */
class FRBSingleton
{
public:
  static void create(size_t) {}
  static size_t num_threads()
  {
    return std::thread::hardware_concurrency();
  }
  static void destroy() {}

private:
  FRBSingleton() = delete;
};
