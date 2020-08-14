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

#pragma once

#include "IBitIO.h"
#include "IBufferedStream.h"

namespace grk {

/*
 Bit input/output
 */
class BitIO: public IBitIO {

public:

	BitIO(uint8_t *bp, uint64_t len, bool isEncoder);
	BitIO(IBufferedStream *stream, bool isEncoder);

	/*
	 Number of bytes written.
	 @return the number of bytes written
	 */
	size_t numbytes();

	/*
	 Write bits
	 @param v Value of bits
	 @param n Number of bits to write
	 */
	bool write(uint32_t v, uint32_t n);
	/*
	 Read bits
	 @param n Number of bits to read
	 */
	void read(uint32_t *bits, uint32_t n);
	/*
	 Flush bits
	 @return true if successful, returns false otherwise
	 */
	bool flush();
	/*
	 Passes the ending bits (coming from flushing)
	 */
	void inalign();

	void simulateOutput(bool doSimulate) {
		sim_out = doSimulate;
	}

	void putcommacode(int32_t n);
	void getcommacode(uint32_t *n);
	void putnumpasses(uint32_t n);
	void getnumpasses(uint32_t *numpasses);

private:

	/* pointer to the start of the buffer */
	uint8_t *start;

	size_t offset;
	size_t buf_len;

	/* temporary place where each byte is read or written */
	uint8_t buf;
	/* coder : number of bits free to write. decoder : number of bits read */
	uint8_t ct;

	size_t total_bytes;

	bool sim_out;

	IBufferedStream *stream;

	/*
	 Write a bit
	 @param bio BIO handle
	 @param b Bit to write (0 or 1)
	 */
	bool putbit(uint8_t b);
	/*
	 Read a bit
	 @param bio BIO handle
	 */
	void getbit(uint32_t *bits, uint8_t pos);
	/*
	 Write a byte
	 @param bio BIO handle
	 @return true if successful, returns false otherwise
	 */
	bool byteout();

	/*
	 Write a byte
	 @param bio BIO handle
	 @return true if successful, returns false otherwise
	 */
	bool byteout_stream();
	/*
	 Read a byte
	 @param bio BIO handle
	 */
	void bytein();

};

}

