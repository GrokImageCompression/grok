#pragma once

namespace grk {

class Wavelet {
public:
	Wavelet();
	virtual ~Wavelet();

	static bool encode(TileComponent *tile_comp, uint8_t qmfbid);

	static bool decode(TileProcessor *p_tcd,  TileComponent* tilec,
	                             uint32_t numres, uint8_t qmfbid);

};

}

