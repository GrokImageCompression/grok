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


#include "grok_includes.h"

#include "dwt_utils.h"
#include "dwt53.h"
#include "dwt97.h"
#include "WaveletForward.h"
#include "dwt.h"

namespace grk {

Wavelet::Wavelet() {
}

Wavelet::~Wavelet() {
}

bool Wavelet::encode(TileComponent *tile_comp, uint8_t qmfbid){
	if (qmfbid == 1) {
		WaveletForward<dwt53> dwt;
		return dwt.run(tile_comp);
	} else if (qmfbid == 0) {
		WaveletForward<dwt97> dwt;
		return dwt.run(tile_comp);
	}
	return false;
}

bool Wavelet::decode(TileProcessor *p_tcd,  TileComponent* tilec,
                             uint32_t numres, uint8_t qmfbid){

	if (qmfbid == 1) {
		return decode_53(p_tcd,tilec,numres);
	} else if (qmfbid == 0) {
		return decode_97(p_tcd,tilec,numres);
	}
	return false;
}

}

