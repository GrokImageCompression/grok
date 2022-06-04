/**
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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

#include "grok.h"

#include <iostream>
#include <cstdint>
#include <limits>
#include <sstream>
#include <atomic>

#include "logger.h"
#include "grk_intmath.h"

namespace grk
{
template<typename T>
struct grk_pt
{
	grk_pt() : x(0), y(0) {}
	grk_pt(T _x, T _y) : x(_x), y(_y) {}
	T x;
	T y;
};
using grk_pt32 = grk_pt<uint32_t>;
using grk_pt16 = grk_pt<uint16_t>;

template<typename T>
struct grk_line
{
	grk_line() : x0(0), x1(0) {}
	grk_line(T _x0, T _x1) : x0(_x0), x1(_x1) {}
	T x0;
	T x1;

	T length() const
	{
		assert(x1 >= x0);
		return (T)(x1 - x0);
	}
};
using grk_line32 = grk_line<uint32_t>;

template<typename T>
struct grk_rect;
using grk_rect32 = grk_rect<uint32_t>;
using grk_rect16 = grk_rect<uint16_t>;
using grk_rect_single = grk_rect<float>;

template<typename T>
T clip(int64_t val)
{
	static_assert(sizeof(T) <= 4);
	if(val < (std::numeric_limits<T>::min)())
		val = (std::numeric_limits<T>::min)();
	else if(val > (std::numeric_limits<T>::max)())
		val = (std::numeric_limits<T>::max)();
	return (T)val;
}

template<typename T>
T satAdd(int64_t lhs, int64_t rhs)
{
	return clip<T>(lhs + rhs);
}

template<typename T>
T satAdd(T lhs, T rhs)
{
	return clip<T>((int64_t)lhs + rhs);
}

template<typename T>
T satSub(T lhs, T rhs)
{
	return clip<T>((int64_t)lhs - rhs);
}

template<typename T>
T satSub(int64_t lhs, int64_t rhs)
{
	return clip<T>(lhs - rhs);
}

template<typename T>
struct grk_rect
{
	grk_rect(T x0, T y0, T x1, T y1) : x0(x0), y0(y0), x1(x1), y1(y1) {}
	grk_rect(const grk_rect& rhs) : grk_rect(&rhs) {}
	grk_rect(const grk_rect* rhs)
	{
		x0 = rhs->x0;
		y0 = rhs->y0;
		x1 = rhs->x1;
		y1 = rhs->y1;
	}
	grk_rect(void) : x0(0), y0(0), x1(0), y1(0) {}
	virtual ~grk_rect() = default;
	T x0, y0, x1, y1;

	virtual void print(void) const
	{
		GRK_INFO("[%u,%u,%u,%u]", x0, y0, x1, y1);
	}
	std::string boundsString() const
	{
		std::ostringstream os;
		os << "[" << x0 << "," << y0 << "," << x1 << "," << y1 << "]";
		return os.str();
	}
	bool valid(void) const
	{
		return x0 <= x1 && y0 <= y1;
	}
	bool empty(void) const
	{
		return x0 >= x1 || y0 >= y1;
	}
	bool contains(grk_pt<T> pt)
	{
		return contains(pt.x, pt.y);
	}
	bool contains(T x, T y)
	{
		return x >= x0 && y >= y0 && x < x1 && y < y1;
	}
	grk_rect<T>& operator=(const grk_rect<T>& rhs)
	{
		return operator=(&rhs);
	}
	grk_rect<T>& operator=(const grk_rect<T>* rhs)
	{
		assert(rhs);
		if(rhs && (this != rhs))
		{ // self-assignment check expected
			x0 = rhs->x0;
			y0 = rhs->y0;
			x1 = rhs->x1;
			y1 = rhs->y1;
		}
		return *this;
	}
	bool operator==(const grk_rect<T>& rhs) const
	{
		if(this == &rhs)
			return true;
		return x0 == rhs.x0 && y0 == rhs.y0 && x1 == rhs.x1 && y1 == rhs.y1;
	}
	void set(grk_rect<T>* rhs)
	{
		*this = *rhs;
	}
	void set(grk_rect<T> rhs)
	{
		set(&rhs);
	}
	grk_rect<T> scaleDownCeil(uint32_t den) const
	{
		return grk_rect<T>(ceildiv(x0, den), ceildiv(y0, den), ceildiv(x1, den), ceildiv(y1, den));
	}
	grk_rect<T> scale(uint32_t scalex, uint32_t scaley) const
	{
		return grk_rect<T>(x0 * scalex, y0 * scaley, x1 * scalex, y1 * scaley);
	}
	grk_rect<T> scaleDown(uint64_t denx, uint64_t deny) const
	{
		return grk_rect<T>((T)(x0 / denx), (T)(y0 / deny), (T)ceildiv<uint64_t>(x1, denx),
						   (T)ceildiv<uint64_t>(y1, deny));
	}
	grk_rect<T> scaleDownPow2(uint32_t powx, uint32_t powy) const
	{
		return grk_rect<T>((T)(x0 >> powx), (T)(y0 >> powy), (T)ceildivpow2<uint64_t>(x1, powx),
						   (T)ceildivpow2<uint64_t>(y1, powy));
	}
	grk_rect<T> scaleDownPow2(grk_pt<T> pow) const
	{
		return scaleDownPow2(pow.x, pow.y);
	}
	grk_rect<T> scaleDownCeil(uint64_t denx, uint64_t deny) const
	{
		return grk_rect<T>(ceildiv<uint64_t>(x0, denx), ceildiv<uint64_t>(y0, deny),
						   ceildiv<uint64_t>(x1, denx), ceildiv<uint64_t>(y1, deny));
	}
	grk_rect<T> scaleDownCeilPow2(uint32_t power) const
	{
		return grk_rect<T>(ceildivpow2(x0, power), ceildivpow2(y0, power), ceildivpow2(x1, power),
						   ceildivpow2(y1, power));
	}
	grk_rect<T> scaleDownCeilPow2(uint32_t powx, uint32_t powy) const
	{
		return grk_rect<T>(ceildivpow2<uint64_t>(x0, powx), ceildivpow2<uint64_t>(y0, powy),
						   ceildivpow2<uint64_t>(x1, powx), ceildivpow2<uint64_t>(y1, powy));
	}
	grk_rect<T> intersection(const grk_rect<T> rhs) const
	{
		return intersection(&rhs);
	}
	bool isContainedIn(const grk_rect<T> rhs) const
	{
		return (intersection(&rhs) == *this);
	}
	grk_rect<T> clip(const grk_rect<T>* rhs) const
	{
		return grk_rect<T>(std::max<T>(x0, rhs->x0), std::max<T>(y0, rhs->y0),
						   std::min<T>(x1, rhs->x1), std::min<T>(y1, rhs->y1));
	}
	grk_rect<T> clip(const grk_rect<T>& rhs) const
	{
		return clip(&rhs);
	}
	// IPL stands for in place
	void clipIPL(const grk_rect<T>* rhs)
	{
		*this = grk_rect<T>(std::max<T>(x0, rhs->x0), std::max<T>(y0, rhs->y0),
							std::min<T>(x1, rhs->x1), std::min<T>(y1, rhs->y1));
	}
	grk_rect<T> intersection(const grk_rect<T>* rhs) const
	{
		return grk_rect<T>(std::max<T>(x0, rhs->x0), std::max<T>(y0, rhs->y0),
						   std::min<T>(x1, rhs->x1), std::min<T>(y1, rhs->y1));
	}
	inline bool nonEmptyIntersection(const grk_rect<T>* rhs) const
	{
		return std::max<T>(x0, rhs->x0) < std::min<T>(x1, rhs->x1) &&
			   std::max<T>(y0, rhs->y0) < std::min<T>(y1, rhs->y1);
	}
	grk_rect<T> rectUnion(const grk_rect<T>* rhs) const
	{
		return grk_rect<T>(std::min<T>(x0, rhs->x0), std::min<T>(y0, rhs->y0),
						   std::max<T>(x1, rhs->x1), std::max<T>(y1, rhs->y1));
	}
	grk_rect<T> rectUnion(const grk_rect<T>& rhs) const
	{
		return rectUnion(&rhs);
	}
	uint64_t area(void) const
	{
		return (uint64_t)(x1 - x0) * (y1 - y0);
	}
	T width() const
	{
		return x1 - x0;
	}
	T height() const
	{
		return y1 - y0;
	}
	grk_line<T> dimX() const
	{
		return grk_line<T>(x0, x1);
	}
	grk_line<T> dimY() const
	{
		return grk_line<T>(y0, y1);
	}
	grk_rect<T> pan(int64_t x, int64_t y) const
	{
		return grk_rect<T>(satAdd<T>((int64_t)x0, (int64_t)x), satAdd<T>((int64_t)y0, (int64_t)y),
						   satAdd<T>((int64_t)x1, (int64_t)x), satAdd<T>((int64_t)y1, (int64_t)y));
	}
	// IPL stands for in place
	grk_rect<T>& growIPL(T boundary)
	{
		return growIPL(boundary, boundary, (std::numeric_limits<T>::max)(),
					   (std::numeric_limits<T>::max)());
	}
	grk_rect<T>& growIPL(T boundaryx, T boundaryy)
	{
		return growIPL(boundaryx, boundaryy, (std::numeric_limits<T>::max)(),
					   (std::numeric_limits<T>::max)());
	}
	grk_rect<T>& growIPL(T boundary, T maxX, T maxY)
	{
		return growIPL(boundary, boundary, maxX, maxY);
	}
	grk_rect<T>& growIPL(T boundaryx, T boundaryy, T maxX, T maxY)
	{
		return growIPL(boundaryx, boundaryy, grk_rect<T>((T)0, (T)0, maxX, maxY));
	}
	grk_rect<T>& growIPL(T boundary, grk_rect<T> bounds)
	{
		return growIPL(boundary, boundary, bounds);
	}
	grk_rect<T>& growIPL(T boundaryx, T boundaryy, grk_rect<T> bounds)
	{
		x0 = std::max<T>(satSub<T>(x0, boundaryx), bounds.x0);
		y0 = std::max<T>(satSub<T>(y0, boundaryy), bounds.y0);
		x1 = std::min<T>(satAdd<T>(x1, boundaryx), bounds.x1);
		y1 = std::min<T>(satAdd<T>(y1, boundaryy), bounds.y1);

		return *this;
	}
	T parityX(void) const
	{
		return T(x0 & 1);
	}
	T parityY(void) const
	{
		return T(y0 & 1);
	}
};

using grk_rect32 = grk_rect<uint32_t>;
using grk_rect16 = grk_rect<uint16_t>;

template<typename T>
void update_maximum(std::atomic<T>& maximum_value, T const& value) noexcept
{
	T prev_value = maximum_value;
	while(prev_value < value && !maximum_value.compare_exchange_weak(prev_value, value))
	{}
}

} // namespace grk
