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

#pragma once

#include "grk_apps_config.h"
#include "grok.h"
#include <string>

class IImageFormat {
public:
	virtual ~IImageFormat() {}
	virtual bool encodeHeader(grk_image *image, const std::string &filename , uint32_t compressionParam)=0;
	virtual bool encodeStrip(uint32_t rows) = 0;
	virtual bool encodeFinish(void) = 0;
	virtual grk_image*  decode(const std::string &filename ,  grk_cparameters  *parameters)=0;

};
