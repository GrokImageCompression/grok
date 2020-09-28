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
#include "FileStreamIO.h"

ImageFormat::ImageFormat() : m_image(nullptr),
							m_rowCount(0),
							m_rowsPerStrip(0),
							m_numStrips(0),
							m_fileStream(nullptr),
							m_useStdIO(false)
{
	m_fileIO = new FileStreamIO();
}

ImageFormat::~ImageFormat() {
	delete m_fileIO;
}


bool ImageFormat::encodeHeader(grk_image *  image, const std::string &filename, uint32_t compressionParam){
	(void)compressionParam;
	m_image = image;

	return m_fileIO->open(filename, "w");
}

bool ImageFormat::encodeFinish(void){
	bool rc =  m_fileIO->close();
	delete m_fileIO;
	m_fileIO = nullptr;

	return rc;
}
bool ImageFormat::openFile(std::string fileName, std::string mode){
	return m_fileIO->open(fileName, mode);
}

bool ImageFormat::writeToFile(uint8_t *buf, size_t len){
	return m_fileIO->write(buf, len);
}
bool ImageFormat::readFromFile(uint8_t *buf, size_t len){
	return m_fileIO->read(buf,len);
}

bool ImageFormat::seekInFile(size_t pos){
	return m_fileIO->seek(pos);
}


uint32_t ImageFormat::maxY(uint32_t rows){
	return std::min<uint32_t>(m_rowCount + rows, m_image->comps[0].h);
}

