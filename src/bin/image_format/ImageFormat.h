/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
#include "IFileIO.h"

class ImageFormat : public IImageFormat
{
  public:
	ImageFormat();
	ImageFormat(const ImageFormat& rhs);
	virtual ~ImageFormat();
	ImageFormat& operator=(const ImageFormat& rhs);
	virtual bool encodeHeader(grk_image* image, const std::string& filename,
							  uint32_t compressionParam) override;
	virtual bool encodeFinish(void) override;

  protected:
	bool open(std::string fname, std::string mode);
	bool write(uint8_t* buf, size_t len);
	bool read(uint8_t* buf, size_t len);
	bool seek(int64_t pos);
	uint32_t maxY(uint32_t rows);
	int getMode(const char* mode);
	void scaleComponent(grk_image_comp* component, uint8_t precision);

	void allocPalette(grk_color* color, uint8_t num_channels, uint16_t num_entries);
	void copy_icc(grk_image* dest, uint8_t* iccbuf, uint32_t icclen);
	void create_meta(grk_image* img);
	bool validate_icc(GRK_COLOR_SPACE colourSpace, uint8_t* iccbuf, uint32_t icclen);

	bool allComponentsSanityCheck(grk_image* image, bool equalPrecision);
	bool isSubsampled(grk_image* image);
	bool isChromaSubsampled(grk_image* image);
	bool areAllComponentsSameSubsampling(grk_image* image);

	grk_image* m_image;
	uint32_t m_rowCount;
	uint32_t m_rowsPerStrip;
	uint32_t m_numStrips;

	IFileIO* m_fileIO;
	FILE* m_fileStream;
	std::string m_fileName;

	bool m_useStdIO;
};
