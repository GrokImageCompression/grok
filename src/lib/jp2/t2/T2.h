#pragma once

namespace grk {

class T2 {
public:
	T2();
	virtual ~T2();
protected:
	/**
	 @param cblk
	 @param index
	 @param cblk_sty
	 @param first
	 */
	bool init_seg(grk_cblk_dec *cblk, uint32_t index, uint8_t cblk_sty,
			bool first);
};

} /* namespace grk */

