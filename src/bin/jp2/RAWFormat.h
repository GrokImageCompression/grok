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

class RAWFormat {
public:
	RAWFormat(bool isBig) : bigEndian(isBig) {}
	virtual ~RAWFormat() {}
	bool encode(grk_image_t* image, const char* filename, int compressionParam, bool verbose);
	grk_image_t* decode(const char* filename, grk_cparameters_t *parameters);
private:
	bool bigEndian;
	grk_image_t* rawtoimage(const char *filename, grk_cparameters_t *parameters, bool big_endian);
	int imagetoraw(grk_image_t * image, 
					const char *outfile,
					bool big_endian, 
					bool verbose);

};