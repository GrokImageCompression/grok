/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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


FileStreamIO::FileStreamIO()  : m_fileHandle(nullptr)
{
}

FileStreamIO::~FileStreamIO()
{
	close();
}

bool FileStreamIO::open(std::string fileName, std::string mode){
	bool useStdio = grk::useStdio(fileName.c_str());
	switch(mode[0]){
	case 'r':
		if (useStdio) {
			if (!grk::grk_set_binary_mode(stdin))
				return false;
			m_fileHandle = stdin;
		} else {
			m_fileHandle = fopen(fileName.c_str(), "rb");
			if (!m_fileHandle) {
				spdlog::error("Failed to open {} for reading", fileName);
				return false;
			}
		}
		break;
	case 'w':
		if (!grk::grk_open_for_output(&m_fileHandle, fileName.c_str(),useStdio))
			return false;
		break;
	}
	m_fileName = fileName;

	return true;

}
bool FileStreamIO::close(void){
	bool rc = false;
	if (!grk::useStdio(m_fileName.c_str()) && m_fileHandle) {
		if (grk::safe_fclose(m_fileHandle))
			rc = true;
	}
	m_fileHandle = nullptr;
	return rc;
}
bool FileStreamIO::write(uint8_t *buf, size_t len){
	auto actual = fwrite(buf, 1, len, m_fileHandle);
	if (actual < len)
		spdlog::error("wrote fewer bytes {} than expected number of bytes {}.",actual, len);

	return actual == len;
}
bool FileStreamIO::read(uint8_t *buf, size_t len){
	auto actual = fread(buf, 1, len, m_fileHandle);
	if (actual < len)
		spdlog::error("read fewer bytes {} than expected number of bytes {}.",actual, len);

	return actual == len;
}
bool FileStreamIO::seek(size_t pos){
	return  fseek(m_fileHandle, pos, SEEK_SET) == 0;
}
