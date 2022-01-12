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

#include "IFileIO.h"

class FileStreamIO : public IFileIO
{
  public:
	FileStreamIO();
	virtual ~FileStreamIO() override;
	bool open(std::string fileName, std::string mode) override;
	bool close(void) override;
	bool write(uint8_t* buf, uint64_t offset,size_t len, size_t maxLen, bool pooled) override;
	bool write(GrkSerializeBuf buffer,
			GrkSerializeBuf* reclaimed,
				uint32_t max_reclaimed,
				uint32_t *num_reclaimed) override;
	bool read(uint8_t* buf, size_t len) override;
	bool seek(int64_t pos) override;
	FILE* getFileStream(void);
	int getFileDescriptor(void);

  private:
	FILE* m_fileHandle;
	std::string m_fileName;
};
