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

#include <FileStreamIO.h>
#include "common.h"

FileStreamIO::FileStreamIO() : m_fileHandle(nullptr) {}

FileStreamIO::~FileStreamIO()
{
	close();
}

bool FileStreamIO::open(std::string fileName, std::string mode)
{
	bool useStdio = grk::useStdio(fileName.c_str());
	switch(mode[0])
	{
		case 'r':
			if(useStdio)
			{
				if(!grk::grk_set_binary_mode(stdin))
					return false;
				m_fileHandle = stdin;
			}
			else
			{
				m_fileHandle = fopen(fileName.c_str(), "rb");
				if(!m_fileHandle)
				{
					spdlog::error("Failed to open {} for reading", fileName);
					return false;
				}
			}
			break;
		case 'w':
			if(!grk::grk_open_for_output(&m_fileHandle, fileName.c_str(), useStdio))
				return false;
			break;
	}
	m_fileName = fileName;

	return true;
}
bool FileStreamIO::close(void)
{
	bool rc = true;
	if(!grk::useStdio(m_fileName.c_str()) && m_fileHandle)
		rc = grk::safe_fclose(m_fileHandle);
	m_fileHandle = nullptr;
	return rc;
}
bool FileStreamIO::write(uint8_t* buf, uint64_t offset,size_t len, size_t maxLen,bool pooled)
{
	(void)offset;
	(void)pooled;
	(void)maxLen;
	auto actual = fwrite(buf, 1, len, m_fileHandle);
	if(actual < len)
		spdlog::error("wrote fewer bytes {} than expected number of bytes {}.", actual, len);

	return actual == len;
}
bool FileStreamIO::write(GrkSerializeBuf buffer,
						GrkSerializeBuf* reclaimed,
						uint32_t max_reclaimed,
						uint32_t *num_reclaimed) {
	auto actual = fwrite(buffer.data, 1, buffer.dataLen, m_fileHandle);
	(void)reclaimed;
	(void)max_reclaimed;
	(void)num_reclaimed;

	if(actual < buffer.dataLen)
		spdlog::error("wrote fewer bytes {} than expected number of bytes {}.", actual, buffer.dataLen);

	return actual == buffer.dataLen;
}
bool FileStreamIO::read(uint8_t* buf, size_t len)
{
	auto actual = fread(buf, 1, len, m_fileHandle);
	if(actual < len)
		spdlog::error("read fewer bytes {} than expected number of bytes {}.", actual, len);

	return actual == len;
}
bool FileStreamIO::seek(int64_t pos)
{
	return GRK_FSEEK(m_fileHandle, pos, SEEK_SET) == 0;
}

FILE* FileStreamIO::getFileStream()
{
	return m_fileHandle;
}
int FileStreamIO::getFileDescriptor(void)
{
#ifndef __WIN32__
	if(m_fileHandle)
		return fileno(m_fileHandle);
#endif
	return 0;
}
