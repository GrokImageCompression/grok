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
 */

#pragma once

#include "IImageFormat.h"
#include "IFileIO.h"
#include "BufferPool.h"
#include "Serializer.h"

#include <mutex>

const uint32_t reclaimSize = 5;

class ImageFormat : public IImageFormat
{
  public:
	ImageFormat();
	virtual ~ImageFormat();
	ImageFormat& operator=(const ImageFormat& rhs);
	virtual bool encodeInit(grk_image* image, const std::string& filename,
							uint32_t compressionLevel) override;
	bool encodePixels(grk_serialize_buf pixels, grk_serialize_buf* reclaimed,
					  uint32_t max_reclaimed, uint32_t* num_reclaimed) override;
	virtual bool encodeFinish(void) override;
	uint32_t getEncodeState(void) override;
	bool openFile(void);

  protected:
	bool open(std::string fname, std::string mode);
	uint64_t write(GrkSerializeBuf buffer);
	bool read(uint8_t* buf, size_t len);
	bool seek(int64_t pos, int whence);
	bool closeStream(void);
	uint32_t maxY(uint32_t rows);
	int getMode(const char* mode);
	void scaleComponent(grk_image_comp* component, uint8_t precision);

	void allocPalette(grk_color* color, uint8_t num_channels, uint16_t num_entries);
	void copy_icc(grk_image* dest, uint8_t* iccbuf, uint32_t icclen);
	void create_meta(grk_image* img);
	bool validate_icc(GRK_COLOR_SPACE colourSpace, uint8_t* iccbuf, uint32_t icclen);

	bool allComponentsSanityCheck(grk_image* image, bool equalPrecision);
	bool isFinalOutputSubsampled(grk_image* image);
	bool isChromaSubsampled(grk_image* image);
	bool areAllComponentsSameSubsampling(grk_image* image);

	uint8_t getImagePrec(void);
	uint16_t getImageNumComps(void);
	GRK_COLOR_SPACE getImageColourSpace(void);

	bool isHeaderEncoded(void);
	bool isOpacity(uint16_t compno);
	bool hasOpacity(void);

	grk_image* image_;
	uint32_t rowCount_;
	uint32_t numStrips_;

	IFileIO* fileIO_;
	FILE* fileStream_;
	std::string fileName_;
	uint32_t compressionLevel_;

	bool useStdIO_;
	uint32_t encodeState;
	uint32_t stripCount;
	mutable std::mutex encodePixelmutex;
	BufferPool pool;
	grk_serialize_buf reclaimed_[reclaimSize];
	uint32_t num_reclaimed_;
	Serializer serializer;
};
