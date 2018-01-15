/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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

#include <stdint.h>

 namespace grk {

struct tcd_tilecomp_t;

class dwt_interface {
public:
	virtual ~dwt_interface() {}
	/**
	Forward wavelet transform in 2-D.
	Apply a reversible DWT transform to a component of an image.
	@param tilec Tile component information (current tile)
	*/
	virtual bool encode(tcd_tilecomp_t * tilec)=0;

	/**
	Inverse wavelet transform in 2-D.
	Apply a reversible inverse DWT transform to a component of an image.
	@param tilec Tile component information (current tile)
	@param numres Number of resolution levels to decode
	*/
	virtual bool decode(tcd_tilecomp_t* tilec,
						uint32_t numres,
						uint32_t numThreads) = 0;
};

}