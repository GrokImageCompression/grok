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
 *
 */

#pragma once

#include <string>

typedef bool (*process_read_func)(void);

class IFileIO
{
  public:
	virtual ~IFileIO() = default;
	virtual bool open(std::string fileName, std::string mode) = 0;
	virtual bool close(void) = 0;
	virtual bool write(uint8_t* buf, uint64_t offset, size_t len) = 0;
	virtual bool read(uint8_t* buf, size_t len) = 0;
	virtual bool seek(int64_t pos) = 0;
};
