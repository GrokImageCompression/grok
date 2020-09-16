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
*/

#pragma once

#include "IImageFormat.h"

class ImageFormat : public IImageFormat {
public:
	ImageFormat();
	virtual ~ImageFormat() {}
	virtual bool encodeHeader(grk_image *image, const std::string &filename, uint32_t compressionParam);
protected:
	grk_image *m_image;
	std::string m_fileName;
	FILE *m_file;
	uint32_t m_rowCount;
	uint32_t m_rowsPerStrip;
	uint32_t m_numStrips;
	bool m_writeToStdout;

	uint32_t maxY(uint32_t rows);

};
