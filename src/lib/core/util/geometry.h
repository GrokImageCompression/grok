/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

#include <iostream>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <sstream>
#include <atomic>
#include <cassert>

#include "grok.h"
#include "Logger.h"
#include "intmath.h"

namespace grk
{
template<typename T>
struct Point
{
  Point() : x(0), y(0) {}
  Point(T _x, T _y) : x(_x), y(_y) {}
  T x;
  T y;
};
using Point32 = Point<uint32_t>;
using Point16 = Point<uint16_t>;
using Point8 = Point<uint8_t>;

template<typename T>
struct Line
{
  Line() : x0(0), x1(0) {}
  Line(T _x0, T _x1) : x0(_x0), x1(_x1) {}
  T x0;
  T x1;

  T length() const
  {
    assert(x1 >= x0);
    return (T)(x1 - x0);
  }
};
using Line32 = Line<uint32_t>;

template<typename T>
struct Rect;
using Rect32 = Rect<uint32_t>;
using Rect16 = Rect<uint16_t>;
using RectF = Rect<float>;
using RectD = Rect<double>;

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

struct Rect32_16
{
  Rect32_16(uint32_t x0, uint32_t y0, uint16_t w, uint16_t h) : x0_(x0), y0_(y0), w_(w), h_(h) {}
  Rect32_16(void) : Rect32_16(0, 0, 0, 0) {}
  virtual ~Rect32_16() = default;
  uint32_t x0(void) const
  {
    return x0_;
  }
  uint32_t y0(void) const
  {
    return y0_;
  }
  uint32_t x1(void) const
  {
    return x0_ + w_;
  }
  uint32_t y1(void) const
  {
    return y0_ + h_;
  }
  uint16_t width() const
  {
    return w_;
  }
  uint16_t height() const
  {
    return h_;
  }
  bool valid() const
  {
    return x0_ <= x1() && y0_ <= y1();
  }
  bool empty(void) const
  {
    return x0_ >= x1() || y0_ >= y1();
  }

  uint32_t area(void) const
  {
    return (uint32_t)w_ * h_;
  }
  Rect32_16 intersection(const Rect32_16* rhs) const
  {
    uint32_t x = std::max<uint32_t>(x0(), rhs->x0());
    uint32_t y = std::max<uint32_t>(y0(), rhs->y0());
    uint16_t w = uint16_t(std::min<uint32_t>(x1(), rhs->x1()) - x);
    uint16_t h = uint16_t(std::min<uint32_t>(y1(), rhs->y1()) - y);
    return Rect32_16(x, y, w, h);
  }
  void setRect(const Rect32_16* rhs)
  {
    *this = *rhs;
  }
  void setRect(const Rect32_16 rhs)
  {
    setRect(&rhs);
  }

private:
  uint32_t x0_, y0_;
  uint16_t w_, h_;
};

template<typename T>
struct Rect
{
  Rect(T origin_x0, T origin_y0, T x0, T y0, T x1, T y1)
      : absoluteCoordinates(true), origin_x0(origin_x0), origin_y0(origin_y0), x0(x0), y0(y0),
        x1(x1), y1(y1)
  {}
  Rect(T x0, T y0, T x1, T y1) : Rect(x0, y0, x0, y0, x1, y1) {}
  Rect(const Rect& rhs) : Rect(&rhs) {}
  explicit Rect(const Rect* rhs)
      : origin_x0(rhs->origin_x0), origin_y0(rhs->origin_y0), x0(rhs->x0), y0(rhs->y0), x1(rhs->x1),
        y1(rhs->y1)
  {
    absoluteCoordinates = rhs->absoluteCoordinates;
  }
  Rect(void) : Rect(0, 0, 0, 0) {}
  virtual ~Rect() = default;

  bool absoluteCoordinates;
  T origin_x0, origin_y0;
  T x0, y0, x1, y1;

  Rect<T>& setOrigin(T origx, T origy, bool absolute)
  {
    absoluteCoordinates = absolute;

    assert(x0 >= origx);
    assert(y0 >= origy);

    origin_x0 = origx;
    origin_y0 = origy;

    return *this;
  }
  Rect<T>& setOrigin(Rect<T>& rhs, bool absolute)
  {
    return setOrigin(&rhs, absolute);
  }
  Rect<T>& setOrigin(const Rect<T>* rhs, bool absolute)
  {
    absoluteCoordinates = absolute;

    if(rhs)
    {
      assert(x0 >= rhs->origin_x0);
      assert(y0 >= rhs->origin_y0);
      origin_x0 = rhs->origin_x0;
      origin_y0 = rhs->origin_y0;
    }

    return *this;
  }
  Rect<T>& toRelative(void)
  {
    assert(x0 >= origin_x0);
    assert(y0 >= origin_y0);
    if(absoluteCoordinates)
      pan_IN_PLACE(-(int64_t)origin_x0, -(int64_t)origin_y0);
    absoluteCoordinates = false;

    return *this;
  }
  Rect<T>& toAbsolute(void)
  {
    if(!absoluteCoordinates)
      pan_IN_PLACE(origin_x0, origin_y0);
    absoluteCoordinates = true;

    return *this;
  }
  virtual void print(void) const
  {
    grklog.info("[%u,%u,%u,%u,%u,%u]", origin_x0, origin_y0, x0, y0, x1, y1);
  }
  bool valid(void) const
  {
    return x0 <= x1 && y0 <= y1;
  }
  bool empty(void) const
  {
    return x0 >= x1 || y0 >= y1;
  }
  bool contains(Point<T> pt)
  {
    return contains(pt.x, pt.y);
  }
  bool contains(T x, T y)
  {
    return x >= x0 && y >= y0 && x < x1 && y < y1;
  }
  Rect<T>& operator=(const Rect& rhs)
  {
    if(*this != rhs)
    { // self-assignment check expected
      absoluteCoordinates = rhs.absoluteCoordinates;
      origin_x0 = rhs.origin_x0;
      origin_y0 = rhs.origin_y0;
      x0 = rhs.x0;
      y0 = rhs.y0;
      x1 = rhs.x1;
      y1 = rhs.y1;
    }
    return *this;
  }
  Rect<T>& operator=(const Rect* rhs)
  {
    if(!rhs)
    {
      throw std::invalid_argument("Null pointer passed to Rect::operator=");
    }
    return *this = *rhs;
  }
  // Define operator!=
  bool operator!=(const Rect& other) const
  {
    return !(*this == other);
  }
  bool operator==(const Rect& rhs) const
  {
    if(this == &rhs)
      return true;
    return absoluteCoordinates == rhs.absoluteCoordinates && origin_x0 == rhs.origin_x0 &&
           origin_y0 == rhs.origin_y0 && x0 == rhs.x0 && y0 == rhs.y0 && x1 == rhs.x1 &&
           y1 == rhs.y1;
  }
  void setRect(const Rect* rhs)
  {
    *this = *rhs;
  }
  void setRect(const Rect rhs)
  {
    setRect(&rhs);
  }
  Rect<T> scaleDownCeil(uint32_t den) const
  {
    return Rect<T>(ceildiv(origin_x0, den), ceildiv(origin_y0, den), ceildiv(x0, den),
                   ceildiv(y0, den), ceildiv(x1, den), ceildiv(y1, den));
  }
  Rect<T> scale(uint32_t scalex, uint32_t scaley) const
  {
    return Rect<T>(origin_x0 * scalex, origin_y0 * scaley, x0 * scalex, y0 * scaley, x1 * scalex,
                   y1 * scaley);
  }
  Rect<T> scaleDown(uint64_t denx, uint64_t deny) const
  {
    return Rect((T)(origin_x0 / denx), (T)(origin_y0 / deny), (T)(x0 / denx), (T)(y0 / deny),
                (T)ceildiv<uint64_t>(x1, denx), (T)ceildiv<uint64_t>(y1, deny));
  }
  Rect<T> scaleDownPow2(uint8_t powx, uint8_t powy) const
  {
    return Rect((T)(origin_x0 >> powx), (T)(origin_y0 >> powy), (T)(x0 >> powx), (T)(y0 >> powy),
                (T)ceildivpow2<uint64_t>(x1, powx), (T)ceildivpow2<uint64_t>(y1, powy));
  }
  Rect<T> scaleDownPow2(Point8 pow) const
  {
    return scaleDownPow2(pow.x, pow.y);
  }
  Rect<T> scaleDownCeil(uint64_t denx, uint64_t deny) const
  {
    return Rect(ceildiv<uint64_t>(origin_x0, denx), ceildiv<uint64_t>(origin_y0, deny),
                ceildiv<uint64_t>(x0, denx), ceildiv<uint64_t>(y0, deny),
                ceildiv<uint64_t>(x1, denx), ceildiv<uint64_t>(y1, deny));
  }
  Rect<T> scaleDownCeilPow2(uint8_t power) const
  {
    return Rect(ceildivpow2(origin_x0, power), ceildivpow2(origin_y0, power),
                ceildivpow2(x0, power), ceildivpow2(y0, power), ceildivpow2(x1, power),
                ceildivpow2(y1, power));
  }
  Rect<T> scaleDownCeilPow2(uint8_t powx, uint8_t powy) const
  {
    return Rect(ceildivpow2<uint64_t>(origin_x0, powx), ceildivpow2<uint64_t>(origin_y0, powy),
                ceildivpow2<uint64_t>(x0, powx), ceildivpow2<uint64_t>(y0, powy),
                ceildivpow2<uint64_t>(x1, powx), ceildivpow2<uint64_t>(y1, powy));
  }
  Rect<T> intersection(const Rect<T> rhs) const
  {
    assert(absoluteCoordinates == rhs.absoluteCoordinates);

    return intersection(&rhs);
  }
  Rect<T> clip(const Rect* rhs) const
  {
    assert(absoluteCoordinates == rhs->absoluteCoordinates);
    return Rect<T>(std::max<T>(x0, rhs->x0), std::max<T>(y0, rhs->y0), std::min<T>(x1, rhs->x1),
                   std::min<T>(y1, rhs->y1));
  }
  Rect<T> clip(const Rect32_16& rhs) const
  {
    return clip(&rhs);
  }
  Rect<T> clip(const Rect32_16* rhs) const
  {
    return Rect<T>(std::max<T>(x0, rhs->x0()), std::max<T>(y0, rhs->y0()),
                   std::min<T>(x1, rhs->x1()), std::min<T>(y1, rhs->y1()));
  }
  Rect<T> clip(const Rect& rhs) const
  {
    return clip(&rhs);
  }
  Rect<T>& clip_IN_PLACE(const Rect& rhs)
  {
    assert(absoluteCoordinates == rhs.absoluteCoordinates);
    *this = Rect<T>(std::max<T>(x0, rhs.x0), std::max<T>(y0, rhs.y0), std::min<T>(x1, rhs.x1),
                    std::min<T>(y1, rhs.y1));

    return *this;
  }
  Rect<T> intersection(const Rect* rhs) const
  {
    assert(absoluteCoordinates == rhs->absoluteCoordinates);
    return Rect<T>(std::max<T>(x0, rhs->x0), std::max<T>(y0, rhs->y0), std::min<T>(x1, rhs->x1),
                   std::min<T>(y1, rhs->y1));
  }
  bool nonEmptyIntersection(const Rect* rhs) const
  {
    assert(absoluteCoordinates == rhs->absoluteCoordinates);
    return std::max<T>(x0, rhs->x0) < std::min<T>(x1, rhs->x1) &&
           std::max<T>(y0, rhs->y0) < std::min<T>(y1, rhs->y1);
  }
  bool nonEmptyIntersection(const Rect32_16* rhs) const
  {
    return std::max<T>(x0, rhs->x0()) < std::min<T>(x1, rhs->x1()) &&
           std::max<T>(y0, rhs->y0()) < std::min<T>(y1, rhs->y1());
  }
  Rect<T> rectUnion(const Rect* rhs) const
  {
    assert(absoluteCoordinates == rhs->absoluteCoordinates);
    return Rect<T>(std::min<T>(x0, rhs->x0), std::min<T>(y0, rhs->y0), std::max<T>(x1, rhs->x1),
                   std::max<T>(y1, rhs->y1));
  }
  Rect<T> rectUnion(const Rect& rhs) const
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
  Line<T> dimX() const
  {
    return Line<T>(x0, x1);
  }
  Line<T> dimY() const
  {
    return Line<T>(y0, y1);
  }
  // pan doesn't affect origin
  Rect<T> pan(int64_t x, int64_t y) const
  {
    auto rc = (*this);

    return rc.pan_IN_PLACE(x, y);
  }
  Rect<T>& pan_IN_PLACE(int64_t x, int64_t y)
  {
    x0 = satAdd<T>((int64_t)x0, (int64_t)x);
    y0 = satAdd<T>((int64_t)y0, (int64_t)y);
    x1 = satAdd<T>((int64_t)x1, (int64_t)x);
    y1 = satAdd<T>((int64_t)y1, (int64_t)y);

    return *this;
  }
  // grow doesn't affect origin
  Rect<T>& grow_IN_PLACE(T boundary)
  {
    return grow_IN_PLACE(boundary, boundary, (std::numeric_limits<T>::max)(),
                         (std::numeric_limits<T>::max)());
  }
  Rect<T>& grow_IN_PLACE(T boundaryx, T boundaryy)
  {
    return grow_IN_PLACE(boundaryx, boundaryy, (std::numeric_limits<T>::max)(),
                         (std::numeric_limits<T>::max)());
  }
  Rect<T>& grow_IN_PLACE(T boundary, T maxX, T maxY)
  {
    return grow_IN_PLACE(boundary, boundary, maxX, maxY);
  }
  Rect<T>& grow_IN_PLACE(T boundaryx, T boundaryy, T maxX, T maxY)
  {
    return grow_IN_PLACE(boundaryx, boundaryy, Rect<T>((T)0, (T)0, maxX, maxY));
  }
  Rect<T>& grow_IN_PLACE(T boundary, Rect<T> bounds)
  {
    return grow_IN_PLACE(boundary, boundary, bounds);
  }
  Rect<T>& grow_IN_PLACE(T boundaryx, T boundaryy, Rect bounds)
  {
    x0 = std::max<T>(satSub<T>(x0, boundaryx), bounds.x0);
    y0 = std::max<T>(satSub<T>(y0, boundaryy), bounds.y0);
    x1 = std::min<T>(satAdd<T>(x1, boundaryx), bounds.x1);
    y1 = std::min<T>(satAdd<T>(y1, boundaryy), bounds.y1);

    return *this;
  }
};

using Rect32 = Rect<uint32_t>;
using Rect16 = Rect<uint16_t>;

} // namespace grk
