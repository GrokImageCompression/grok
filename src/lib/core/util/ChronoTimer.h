/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include <chrono>
#include <string>

#include "Logger.h"

namespace grk
{

class ChronoTimer
{
public:
  ChronoTimer(const std::string& msg) : message(msg) {}
  void start(void)
  {
    startTime = std::chrono::high_resolution_clock::now();
  }
  void finish(void)
  {
    auto finish = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = finish - startTime;
    grklog.info("%s : %f ms", message.c_str(), elapsed.count() * 1000);
  }

private:
  std::string message;
  std::chrono::high_resolution_clock::time_point startTime;
};

} // namespace grk
