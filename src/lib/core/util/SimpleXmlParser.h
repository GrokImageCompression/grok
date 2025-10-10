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

#include <string>
#include <vector>

namespace grk
{

// Simple XML parser for S3 ListObjectsV2 response
class SimpleXmlParser
{
public:
  std::vector<std::string> keys;

  bool parse(const std::string& xml)
  {
    keys.clear();
    size_t pos = 0;
    while((pos = xml.find("<Key>", pos)) != std::string::npos)
    {
      pos += 5;
      size_t end = xml.find("</Key>", pos);
      if(end == std::string::npos)
        break;
      keys.push_back(xml.substr(pos, end - pos));
      pos = end + 6;
    }
    return !keys.empty();
  }
};

} // namespace grk