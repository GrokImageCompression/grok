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

#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <optional>

namespace grk
{

class EnvVarManager
{
public:
  // Retrieve an environment variable as a string (returns empty optional if unset)
  static std::optional<std::string> get(const char* name)
  {
    const char* value = std::getenv(name);
    if(value && *value)
    {
      return std::string(value);
    }
    return std::nullopt;
  }

  // Test if a variable is a boolean (mimics GDAL's CPLTestBool)
  static bool test_bool(const char* name, bool default_value = false)
  {
    auto value = get(name);
    if(!value)
    {
      return default_value;
    }
    const char* str = value->c_str();
    if(!str || !*str)
    {
      return default_value;
    }

    // Case-insensitive comparison
    char buffer[16];
    size_t i = 0;
    for(; str[i] && i < sizeof(buffer) - 1; ++i)
    {
      buffer[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(str[i])));
    }
    buffer[i] = '\0';

    return std::strcmp(buffer, "true") == 0 || std::strcmp(buffer, "on") == 0 ||
           std::strcmp(buffer, "yes") == 0 || std::strcmp(buffer, "1") == 0;
  }

  // Get an environment variable as an integer (returns default_value if unset or invalid)
  static long get_int(const char* name, long default_value = 0)
  {
    auto value = get(name);
    if(!value)
    {
      return default_value;
    }
    try
    {
      return std::stol(*value);
    }
    catch(...)
    {
      return default_value;
    }
  }

  // Get an environment variable as a string (returns default_value if unset)
  static std::string get_string(const char* name, const std::string& default_value = "")
  {
    auto value = get(name);
    return value.value_or(default_value);
  }
};

} // namespace grk