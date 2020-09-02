/*
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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"

namespace grk {

BitIO::BitIO(uint8_t *bp, uint64_t len, bool isEncoder) :
		start(bp), offset(0), buf_len(len), buf(0), ct(isEncoder ? 8 : 0), total_bytes(
				0), sim_out(false), stream(nullptr) {

}

BitIO::BitIO(IBufferedStream *strm, bool isEncoder) :
		start(nullptr), offset(0), buf_len(0), buf(0), ct(isEncoder ? 8 : 0), total_bytes(
				0), sim_out(false), stream(strm) {
}

bool BitIO::byteout() {
	if (stream)
		return byteout_stream();
	if (offset == buf_len) {
		if (!sim_out)
			assert(false);
		return false;
	}
	ct = buf == 0xff ? 7 : 8;
	if (!sim_out)
		start[offset] = buf;
	offset++;
	buf = 0;
	return true;
}

bool BitIO::byteout_stream() {
	if (!stream->write_byte(buf))
		return false;
	ct = buf == 0xff ? 7 : 8;
	buf = 0;
	return true;
}

void BitIO::bytein() {
	if (offset == buf_len)
		throw TruncatedStreamException();
	ct = buf == 0xff ? 7 : 8;
	buf = start[offset];
	offset++;
}

bool BitIO::putbit(uint8_t b) {
	if (ct == 0) {
		if (!byteout())
			return false;
	}
	ct--;
	buf = static_cast<uint8_t>(buf | (b << ct));
	return true;
}

void BitIO::getbit(uint32_t *bits, uint8_t pos) {
	if (ct == 0)
		bytein();
	assert(ct > 0);
	ct = (uint8_t)(ct-1);
	*bits |= (uint32_t)(((buf >> ct) & 1) << pos);
}

size_t BitIO::numbytes() {
	return total_bytes + offset;
}

bool BitIO::write(uint32_t v, uint32_t n) {
	assert(n != 0 && n <= 32);
	for (int32_t i = (int32_t)(n - 1); i >= 0; i--) {
		if (!putbit((v >> i) & 1))
			return false;
	}
	return true;
}

void BitIO::read(uint32_t *bits, uint32_t n) {
	assert(n != 0 && n <= 32U);
#ifdef GRK_UBSAN_BUILD
	/* This assert fails for some corrupted images which are gracefully rejected */
	/* Add this assert only for ubsan build. */
	/* This is the condition for overflow not to occur below which is needed because of GROK_NOSANITIZE */
	assert(n <= 32U);
#endif
	*bits = 0U;
	for (int32_t i = (int32_t)(n - 1); i >= 0; i--)
		getbit(bits, static_cast<uint8_t>(i));
}

bool BitIO::flush() {
	if (!byteout())
		return false;
	if (ct == 7) {
		if (!byteout())
			return false;
	}
	return true;
}

void BitIO::inalign() {
	if (buf == 0xff)
		bytein();
	ct = 0;
}

void BitIO::putcommacode(int32_t n) {
	while (--n >= 0)
		write(1, 1);
	write(0, 1);
}

void BitIO::getcommacode(uint32_t *n) {
	*n = 0;
	uint32_t temp;
	read(&temp, 1);
	while (temp){
		++*n;
		read(&temp, 1);
	}
}

void BitIO::putnumpasses(uint32_t n) {
	if (n == 1)
		write(0, 1);
	else if (n == 2)
		write(2, 2);
	else if (n <= 5)
		write(0xc | (n - 3), 4);
	else if (n <= 36)
		write(0x1e0 | (n - 6), 9);
	else if (n <= 164)
		write(0xff80 | (n - 37), 16);
}

void BitIO::getnumpasses(uint32_t *numpasses) {
	uint32_t n = 0;
	read(&n, 1);
	if (!n) {
		*numpasses = 1;
		return;
	}
	read(&n, 1);
	if (!n) {
		*numpasses = 2;
		return;
	}
	read(&n, 2);
	if (n != 3) {
		*numpasses = n + 3;
		return;
	}
	read(&n, 5);
	if (n != 31) {
		*numpasses = n + 6;
		return;
	}
	read(&n, 7);
	*numpasses = n + 37;
}




}
