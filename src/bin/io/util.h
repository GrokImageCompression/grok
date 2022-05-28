/*
 *    Copyright (C) 2022 Grok Image Compression Inc.
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

#include <cstdint>

namespace io
{

struct BufDim
{
	BufDim() : BufDim(0, 0) {}
	BufDim(uint64_t x0, uint64_t x1) : x0_(x0), x1_(x1) {}
	uint64_t len(void)
	{
		return x1_ - x0_;
	}
	uint64_t x0(void)
	{
		return x0_;
	}
	uint64_t x1(void)
	{
		return x1_;
	}
	bool valid(void)
	{
		return x1_ >= x0_;
	}
	bool empty(void)
	{
		assert(valid());
		return x1_ == x0_;
	}
	BufDim intersection(BufDim& rhs)
	{
		if(!rhs.valid() || rhs.x1_ < x0_ || rhs.x0_ > x1_)
			return BufDim();
		return BufDim(std::max(x0_, rhs.x0_), std::min(x1_, rhs.x1_));
	}
	bool operator==(BufDim& rhs)
	{
		return x0_ == rhs.x0_ && x1_ == rhs.x1_;
	}
	uint64_t x0_;
	uint64_t x1_;
};

} // namespace io
