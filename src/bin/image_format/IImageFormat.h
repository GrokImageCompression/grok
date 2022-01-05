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
 */

#pragma once

#include "grk_apps_config.h"
#include "grok.h"
#include <string>

const uint32_t IMAGE_FORMAT_UNENCODED = 1;
const uint32_t IMAGE_FORMAT_ENCODED_HEADER = 2;
const uint32_t 	IMAGE_FORMAT_ENCODED_PIXELS = 4;
const uint32_t 	IMAGE_FORMAT_ERROR = 8;



class IImageFormat
{
  public:
	virtual ~IImageFormat() = default;
	virtual bool initEncode(const std::string& filename,uint32_t compressionLevel) = 0;
	virtual bool encodeHeader(grk_image* image) = 0;
	virtual bool encodeRows(uint32_t rows) = 0;
	virtual bool encodePixels(uint8_t *data, uint64_t dataLen, uint32_t strip) = 0;
	virtual bool encodeFinish(void) = 0;
	virtual grk_image* decode(const std::string& filename, grk_cparameters* parameters) = 0;
	virtual uint32_t getEncodeState(void) = 0;
};
