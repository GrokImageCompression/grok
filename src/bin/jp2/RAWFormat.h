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
	bool encode(opj_image_t* image, std::string filename, int compressionParam, bool verbose);
	opj_image_t* decode(std::string filename, opj_cparameters_t *parameters);
private:
	bool bigEndian;
	opj_image_t* decode_common(const char *filename, opj_cparameters_t *parameters, bool big_endian);
	int encode_common(opj_image_t * image, const char *outfile, bool big_endian);

};