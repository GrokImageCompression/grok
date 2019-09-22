/*
 *    Copyright (C) 2016-2019 Grok Image Compression Inc.
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

namespace grk {

struct IGrokStream {

	virtual ~IGrokStream() {
	}

	virtual bool write_byte(uint8_t value)=0;
	virtual bool write_short(uint16_t value) = 0;
	virtual bool write_24(uint32_t value)=0;
	virtual bool write_int(uint32_t value) = 0;

	/**
	 * Writes some bytes to the stream.
	 * @param		p_buffer	pointer to the data buffer holds the data to be written.
	 * @param		p_size		number of bytes to write.

	 * @return		the number of bytes written, or -1 if an error occurred.
	 */
	virtual size_t write_bytes(const uint8_t *p_buffer, size_t p_size)= 0;

	/**
	 * Writes the content of the stream buffer to the stream.
	 * @return		true if the data could be flushed, false else.
	 */
	virtual bool flush()= 0;

	/**
	 * Skips a number of bytes from the stream.
	 * @param		p_size		the number of bytes to skip.

	 * @return		the number of bytes skipped, or -1 if an error occurred.
	 */
	virtual bool skip(int64_t p_size)= 0;

	/**
	 * Tells the byte offset on the stream (similar to ftell).
	 * @return		the current position o fthe stream.
	 */
	virtual uint64_t tell(void)= 0;

	/**
	 * Get the number of bytes left before the end of the stream
	 * @return		Number of bytes left before the end of the stream.
	 */
	virtual uint64_t get_number_byte_left(void)= 0;

	/**
	 * Skips a number of bytes from the stream.
	 * @param		p_size		the number of bytes to skip.

	 * @return		the number of bytes skipped, or -1 if an error occurred.
	 */
	virtual bool write_skip(int64_t p_size)= 0;

	/**
	 * Skips a number of bytes from the stream.
	 * @param		p_size		the number of bytes to skip.

	 * @return		the number of bytes skipped, or -1 if an error occurred.
	 */
	virtual bool read_skip(int64_t p_size)= 0;

	/**
	 * Seeks to absolute offset in stream.
	 * @param		offset		absolute stream offset

	 * @return		true if success, or false if an error occurred.
	 */
	virtual bool read_seek(uint64_t offset)= 0;

	/**
	 * Seeks to absolute offset in stream.
	 * @param		offset		absolute offset in stream

	 * @return		the number of bytes skipped, or -1 if an error occurred.
	 */
	virtual bool write_seek(uint64_t offset)= 0;

	/**
	 * Seeks to absolute offset in stream.
	 * @param		offset		absolute offset in stream

	 * @return		true if the stream is seekable.
	 */
	virtual bool seek(uint64_t offset)= 0;

	/**
	 * Tells if the given stream is seekable.
	 */
	virtual bool has_seek()= 0;

};

}
