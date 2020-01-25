#pragma once

namespace grk {

class Wavelet {
public:
	Wavelet();
	virtual ~Wavelet();

	static bool encode(grk_tcd_tilecomp *tile_comp, uint8_t qmfbid);

	static bool decode(TileProcessor *p_tcd,  grk_tcd_tilecomp* tilec,
	                             uint32_t numres, uint8_t qmfbid);

};

}

