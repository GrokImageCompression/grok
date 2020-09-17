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
 */

#include "ImageFormat.h"
#include "convert.h"
#include <algorithm>
#include "common.h"

ImageFormat::ImageFormat() : m_image(nullptr),
							m_fileHandle(nullptr),
							m_rowCount(0),
							m_rowsPerStrip(0),
							m_numStrips(0)
{}

bool ImageFormat::encodeHeader(grk_image *  image, const std::string &filename, uint32_t compressionParam){
	m_fileName = filename;
	m_image = image;

	return true;
}

bool ImageFormat::encodeFinish(void){
	if (!grk::useStdio(m_fileName.c_str()) && m_fileHandle) {
		if (!grk::safe_fclose(m_fileHandle))
			return false;
	}

	return true;
}
bool ImageFormat::openFile(std::string mode){
	bool useStdio = grk::useStdio(m_fileName.c_str());
	switch(mode[0]){
	case 'r':
		if (useStdio) {
			if (!grk::grk_set_binary_mode(stdin))
				return false;
			m_fileHandle = stdin;
		} else {
			m_fileHandle = fopen(m_fileName.c_str(), "rb");
			if (!m_fileHandle) {
				spdlog::error("Failed to open {} for reading", m_fileName);
				return false;
			}
		}
		break;
	case 'w':
		if (!grk::grk_open_for_output(&m_fileHandle, m_fileName.c_str(),useStdio))
			return false;
		break;
	}

	return true;
}

bool ImageFormat::writeToFile(uint8_t *buf, size_t len){
	return (fwrite(buf, 1, len, m_fileHandle) == len);
}


uint32_t ImageFormat::maxY(uint32_t rows){
	return std::min<uint32_t>(m_rowCount + rows, m_image->comps[0].h);
}

