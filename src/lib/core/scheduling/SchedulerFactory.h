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

#include <cstdlib>
#include <string>

namespace grk
{

class Scheduling
{
public:
  static bool isWindowedScheduling()
  {
    const char* env = std::getenv("GRK_WINDOWED_SCHEDULING");
    if(!env)
    {
      return false; // Unset variable
    }
    try
    {
      std::string value(env);
      size_t pos;
      int num = std::stoi(value, &pos);
      // Ensure entire string was consumed and no trailing characters
      return pos == value.length() && num != 0;
    }
    catch(const std::exception&)
    {
      return false; // Invalid number
    }
  }
};

} // namespace grk
