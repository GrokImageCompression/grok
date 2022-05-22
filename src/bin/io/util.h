#pragma once

#include <cstdint>

namespace iobench {

struct BufDim {
	BufDim() : BufDim(0,0)
	{}
	BufDim(uint64_t x0, uint64_t x1) : x0_(x0), x1_(x1)
	{}
	uint64_t len(void){
		return x1_ - x0_;
	}
	uint64_t x0(void){
		return x0_;
	}
	uint64_t x1(void){
		return x1_;
	}
	bool valid(void){
		return x1_ >= x0_;
	}
	bool empty(void){
		assert(valid());
		return x1_ == x0_;
	}
	BufDim intersection(BufDim &rhs){
		if (!rhs.valid() || rhs.x1_ < x0_ ||rhs.x0_ > x1_)
			return BufDim();
		return BufDim(std::max(x0_,rhs.x0_), std::min(x1_, rhs.x1_));
	}
	bool operator==(BufDim &rhs){
		return x0_ == rhs.x0_ && x1_ == rhs.x1_;
	}
	uint64_t x0_;
	uint64_t x1_;
};

}
