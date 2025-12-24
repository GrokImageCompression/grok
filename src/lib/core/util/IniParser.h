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

#include <string>
#include <map>
#include <fstream>

namespace grk
{

// Simple INI parser for AWS credentials and config files
class IniParser
{
public:
  std::map<std::string, std::map<std::string, std::string>> sections;

  bool parse(const std::string& filename)
  {
    std::ifstream file(filename);
    if(!file.is_open())
    {
      grklog.debug("Failed to open file: %s", filename.c_str());
      return false;
    }

    std::string current_section;
    std::string line;
    while(std::getline(file, line))
    {
      // Trim whitespace
      line.erase(0, line.find_first_not_of(" \t"));
      line.erase(line.find_last_not_of(" \t") + 1);

      // Skip empty lines and comments
      if(line.empty() || line[0] == ';' || line[0] == '#')
        continue;

      // Check for section header
      if(line[0] == '[' && line.back() == ']')
      {
        current_section = line.substr(1, line.size() - 2);
        sections[current_section] = {};
        continue;
      }

      // Parse key-value pairs
      size_t eq_pos = line.find('=');
      if(eq_pos != std::string::npos)
      {
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        if(!current_section.empty() && !key.empty())
        {
          sections[current_section][key] = value;
        }
      }
    }
    file.close();
    return true;
  }
};

} // namespace grk