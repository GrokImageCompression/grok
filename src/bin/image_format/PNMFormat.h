/*
 *    Copyright (C) 2016 Grok Image Compression Inc.
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

#include "ImageFormat.h"
#include "Serializer.h"

class PNMFormat : public ImageFormat
{
  public:
	explicit PNMFormat(bool split);
	bool encodeHeader(void) override;
	bool encodePixels(void) override;
	bool encodePixels(grk_serialize_buf pixels, grk_serialize_buf* reclaimed,
					  uint32_t max_reclaimed, uint32_t* num_reclaimed) override;
	bool encodeFinish(void) override;
	grk_image* decode(const std::string& filename, grk_cparameters* parameters) override;

  private:
	bool forceSplit;
	Serializer serializer;
	bool hasAlpha(void);
	bool doNonSplitEncode(void);
	bool writeHeader(bool doPGM);
	template<typename T>
	bool writeRows(uint32_t rowsOffset, uint32_t rows, uint16_t compno, T* buf, size_t* outCount);
	template<typename T>
	bool encodeRowsCore(uint32_t rows);

	bool encodePixelsApplication(grk_serialize_buf pixels);
	bool encodePixelsCore(grk_serialize_buf pixels, grk_serialize_buf* reclaimed,
						  uint32_t max_reclaimed, uint32_t* num_reclaimed);

	grk_image* decode(grk_cparameters* parameters);
	bool decodeHeader(struct pnm_header* ph);
};
