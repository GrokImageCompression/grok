#include "grok_includes.h"
#include <T2.h>

namespace grk {

T2::T2() {
	// TODO Auto-generated constructor stub

}

T2::~T2() {
	// TODO Auto-generated destructor stub
}


bool T2::init_seg(grk_cblk_dec *cblk, uint32_t index, uint8_t cblk_sty,
		bool first) {
	uint32_t nb_segs = index + 1;

	if (nb_segs > cblk->numSegmentsAllocated) {
		auto new_segs = new grk_seg[cblk->numSegmentsAllocated
				+ cblk->numSegmentsAllocated];
		for (uint32_t i = 0; i < cblk->numSegmentsAllocated; ++i)
			new_segs[i] = cblk->segs[i];
		cblk->numSegmentsAllocated += default_numbers_segments;
		if (cblk->segs)
			delete[] cblk->segs;
		cblk->segs = new_segs;
	}

	auto seg = &cblk->segs[index];
	seg->clear();

	if (cblk_sty & GRK_CBLKSTY_TERMALL) {
		seg->maxpasses = 1;
	} else if (cblk_sty & GRK_CBLKSTY_LAZY) {
		if (first) {
			seg->maxpasses = 10;
		} else {
			auto last_seg = seg - 1;
			seg->maxpasses =
					((last_seg->maxpasses == 1) || (last_seg->maxpasses == 10)) ?
							2 : 1;
		}
	} else {
		seg->maxpasses = max_passes_per_segment;
	}

	return true;
}



} /* namespace grk */
