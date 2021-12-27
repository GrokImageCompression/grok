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
#include "logger.h"
#include <iostream>
#include <cstdint>
#include "grk_intmath.h"
#include <limits>
#include <sstream>

namespace grk
{
template<typename T>
struct grkPoint
{
	grkPoint() : x(0), y(0) {}
	grkPoint(T _x, T _y) : x(_x), y(_y) {}
	T x;
	T y;
};
using grkPointU32 = grkPoint<uint32_t>;

template<typename T>
struct grkLine
{
	grkLine() : x0(0), x1(0) {}
	grkLine(T _x0, T _x1) : x0(_x0), x1(_x1) {}
	T x0;
	T x1;
	T length()
	{
		assert(x1 >= x0);
		return (T)(x1 - x0);
	}
};
using grkLineU32 = grkLine<uint32_t>;

template<typename T>
struct grkRect;
using grkRectU32 = grkRect<uint32_t>;

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
struct grkRect
{
	T x0, y0, x1, y1;

	grkRect(T x0, T y0, T x1, T y1) : x0(x0), y0(y0), x1(x1), y1(y1) {}
	grkRect(const grkRect& rhs)
	{
		x0 = rhs.x0;
		y0 = rhs.y0;
		x1 = rhs.x1;
		y1 = rhs.y1;
	}
	grkRect(void) : x0(0), y0(0), x1(0), y1(0) {}
	void print(void) const
	{
		std::cout << "[" << x0 << "," << y0 << "," << x1 << "," << y1 << "]" << std::endl;
	}
	std::string boundsString()
	{
		std::ostringstream os;
		os << "[" << x0 << "," << y0 << "," << x1 << "," << y1 << "]";
		return os.str();
	}
	bool is_valid(void) const
	{
		return x0 <= x1 && y0 <= y1;
	}
	bool non_empty(void) const
	{
		return x0 < x1 && y0 < y1;
	}
	bool contains(grkPoint<T> pt)
	{
		return pt.x >= x0 && pt.y >= y0 && pt.x < x1 && pt.y < y1;
	}
	grkRect<T>& operator=(const grkRect<T>& rhs)
	{
		return operator=(&rhs);
	}
	grkRect<T>& operator=(const grkRect<T>* rhs)
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
	bool operator==(const grkRect<T>& rhs) const
	{
		if(this == &rhs)
			return true;
		return x0 == rhs.x0 && y0 == rhs.y0 && x1 == rhs.x1 && y1 == rhs.y1;
	}
	void set(grkRect<T>* rhs)
	{
		*this = *rhs;
	}
	void set(grkRect<T> rhs)
	{
		set(&rhs);
	}
	grkRect<T> rectceildivpow2(uint32_t power) const
	{
		return grkRect<T>(ceildivpow2(x0, power), ceildivpow2(y0, power), ceildivpow2(x1, power),
						  ceildivpow2(y1, power));
	}
	grkRect<T> rectceildiv(uint32_t den) const
	{
		return grkRect<T>(ceildiv(x0, den), ceildiv(y0, den), ceildiv(x1, den), ceildiv(y1, den));
	}
	grkRect<T> rectceildiv(uint32_t denx, uint32_t deny) const
	{
		return grkRect<T>(ceildiv(x0, denx), ceildiv(y0, deny), ceildiv(x1, denx),
						  ceildiv(y1, deny));
	}
	grkRect<T> intersection(const grkRect<T> rhs) const
	{
		return intersection(&rhs);
	}
	bool isContainedIn(const grkRect<T> rhs) const
	{
		return (intersection(&rhs) == *this);
	}
	void clip(const grkRect<T>* rhs)
	{
		*this = grkRect<T>(std::max<T>(x0, rhs->x0), std::max<T>(y0, rhs->y0),
						   std::min<T>(x1, rhs->x1), std::min<T>(y1, rhs->y1));
	}
	grkRect<T> intersection(const grkRect<T>* rhs) const
	{
		return grkRect<T>(std::max<T>(x0, rhs->x0), std::max<T>(y0, rhs->y0),
						  std::min<T>(x1, rhs->x1), std::min<T>(y1, rhs->y1));
	}
	inline bool non_empty_intersection(const grkRect<T>* rhs) const
	{
		return std::max<T>(x0, rhs->x0) < std::min<T>(x1, rhs->x1) &&
			   std::max<T>(y0, rhs->y0) < std::min<T>(y1, rhs->y1);
	}
	grkRect<T> rectUnion(const grkRect<T>* rhs) const
	{
		return grkRect<T>(std::min<T>(x0, rhs->x0), std::min<T>(y0, rhs->y0),
						  std::max<T>(x1, rhs->x1), std::max<T>(y1, rhs->y1));
	}
	grkRect<T> rectUnion(const grkRect<T>& rhs) const
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
	grkLine<T> dimX()
	{
		return grkLine<T>(x0, x1);
	}
	grkLine<T> dimY()
	{
		return grkLine<T>(y0, y1);
	}
	grkRect<T> pan(int64_t x, int64_t y) const
	{
		auto rc = *this;
		rc.panInplace(x, y);
		return rc;
	}
	void panInplace(int64_t x, int64_t y)
	{
		x0 = satAdd<T>((int64_t)x0, (int64_t)x);
		y0 = satAdd<T>((int64_t)y0, (int64_t)y);
		x1 = satAdd<T>((int64_t)x1, (int64_t)x);
		y1 = satAdd<T>((int64_t)y1, (int64_t)y);
	}
	grkRect<T>& grow(T boundary)
	{
		return grow(boundary, boundary, (std::numeric_limits<T>::max)(),
					(std::numeric_limits<T>::max)());
	}
	grkRect<T>& grow(T boundaryx, T boundaryy)
	{
		return grow(boundaryx, boundaryy, (std::numeric_limits<T>::max)(),
					(std::numeric_limits<T>::max)());
	}
	grkRect<T>& grow(T boundary, T maxX, T maxY)
	{
		return grow(boundary, boundary, maxX, maxY);
	}
	grkRect<T>& grow(T boundaryx, T boundaryy, T maxX, T maxY)
	{
		return grow(boundaryx, boundaryy, grkRect<T>((T)0, (T)0, maxX, maxY));
	}
	grkRect<T>& grow(T boundary, grkRect<T> bounds)
	{
		return grow(boundary, boundary, bounds);
	}
	grkRect<T>& grow(T boundaryx, T boundaryy, grkRect<T> bounds)
	{
		x0 = std::max<T>(satSub<T>(x0, boundaryx), bounds.x0);
		y0 = std::max<T>(satSub<T>(y0, boundaryy), bounds.y0);
		x1 = std::min<T>(satAdd<T>(x1, boundaryx), bounds.x1);
		y1 = std::min<T>(satAdd<T>(y1, boundaryy), bounds.y1);

		return *this;
	}
	T parityX(void)
	{
		return T(x0 & 1);
	}
	T parityY(void)
	{
		return T(y0 & 1);
	}
};

using grkRectU32 = grkRect<uint32_t>;

} // namespace grk
