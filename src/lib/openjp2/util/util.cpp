/**
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

namespace grk {

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

/**
 Divide an integer and round upwards
 @return a divided by b
 */
static inline int64_t int64_ceildiv(int64_t a, int64_t b) {
	assert(b);
	return (a + b - 1) / b;
}

void grk_rect::print(void) {
	std::cout << "[" << x0 << "," << y0 << "," << x1 << "," << y1 << "]"
			<< std::endl;
}

grk_rect::grk_rect(void) :
		x0(0), y0(0), x1(0), y1(0) {
}

grk_rect::grk_rect(int64_t x0, int64_t y0, int64_t x1, int64_t y1) :
		x0(x0), y0(y0), x1(x1), y1(y1) {
}

bool grk_rect::is_valid(void) {
	return x0 <= x1 && y0 <= y1;
}

bool grk_rect::is_non_degenerate(void) {
	return x0 < x1 && y0 < y1;
}

bool grk_rect::are_equal(grk_rect *r2) {

	if (!r2)
		return false;

	return x0 == r2->x0 && y0 == r2->y0 && x1 == r2->x1 && y1 == r2->y1;
}

bool grk_rect::clip(grk_rect *r2, grk_rect *result) {
	bool rc;
	grk_rect temp;

	if (!r2 || !result)
		return false;

	temp.x0 = MAX(x0, r2->x0);
	temp.y0 = MAX(y0, r2->y0);

	temp.x1 = MIN(x1, r2->x1);
	temp.y1 = MIN(y1, r2->y1);

	rc = temp.is_valid();

	if (rc)
		*result = temp;
	return rc;
}

void grk_rect::ceildivpow2(uint32_t power) {
	x0 = int64_ceildivpow2(x0, power);
	y0 = int64_ceildivpow2(y0, power);
	x1 = int64_ceildivpow2(x1, power);
	y1 = int64_ceildivpow2(y1, power);

}

void grk_rect::mulpow2(uint32_t power) {
	if (power == 0)
		return;
	x0 *= 1 << power;
	y0 *= 1 << power;
	x1 *= 1 << power;
	y1 *= 1 << power;

}

int64_t grk_rect::get_area(void) {
	return (x1 - x0) * (y1 - y0);
}

void grk_rect::pan(grk_pt *shift) {
	x0 += shift->x;
	y0 += shift->y;
	x1 += shift->x;
	y1 += shift->y;
}

void grk_rect::subsample(uint32_t dx, uint32_t dy) {
	x0 = int64_ceildiv(x0, (int64_t) dx);
	y0 = int64_ceildiv(y0, (int64_t) dy);
	x1 = int64_ceildiv(x1, (int64_t) dx);
	y1 = int64_ceildiv(y1, (int64_t) dy);
}

void grk_rect::grow(int64_t boundary) {
	grow2(boundary, boundary);
}

void grk_rect::grow2(int64_t boundaryx, int64_t boundaryy) {

	x0 -= boundaryx;
	y0 -= boundaryy;
	x1 += boundaryx;
	y1 += boundaryy;
}

uint32_t hardware_concurrency() {
	uint32_t ret = 0;

#if _MSC_VER >= 1200 && MSC_VER <= 1910
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	ret = sysinfo.dwNumberOfProcessors;

#else
	ret = std::thread::hardware_concurrency();
#endif
	return ret;
}


grk_buf::~grk_buf() {
	if (buf && owns_data)
		delete[] buf;
}

void grk_buf::incr_offset(uint64_t off) {
	/*  we allow the offset to move to one location beyond end of buffer segment*/
	if (offset + off > (uint64_t) len) {
#ifdef DEBUG_SEG_BUF
       GROK_WARN("attempt to increment buffer offset out of bounds");
#endif
		offset = (uint64_t) len;
	}
	offset += off;
}

uint8_t* grk_buf::curr_ptr(){
	if (!buf)
		return nullptr;
	return buf + offset;
}

void grk_buf::grow(){
	if (!owns_data)
		return;
	auto temp = new uint8_t[len*2];
	memcpy(temp, buf, len);
	len *=2;
	delete buf;
	buf = temp;
}

}
