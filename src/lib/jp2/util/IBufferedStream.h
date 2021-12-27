/*
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
 */

#pragma once

namespace grk
{
struct IBufferedStream
{
	virtual ~IBufferedStream() = default;

	virtual bool supportsZeroCopy() = 0;
	virtual uint8_t* getZeroCopyPtr() = 0;
	/**
	 * Reads some bytes from the stream.
	 * @param		buffer	pointer to the data buffer
	 * 							that will receive the data.
	 * 							If null, then a zero copy read
	 * 							is performed
	 * @param		p_size		number of bytes to read.

	 * @return		the number of bytes read
	 */
	virtual size_t read(uint8_t* buffer, size_t p_size) = 0;

	// low level write methods
	virtual bool writeShort(uint16_t value) = 0;
	virtual bool write24(uint32_t value) = 0;
	virtual bool writeInt(uint32_t value) = 0;
	virtual bool write64(uint64_t value) = 0;

	virtual bool writeByte(uint8_t value) = 0;
	/**
	 * Write bytes to the stream.
	 * @param		buffer	pointer to the data buffer to be written.
	 * @param		p_size		number of bytes to write.
	 *
	 * @return		the number of bytes written, or -1 if an error occurred.
	 */
	virtual size_t writeBytes(const uint8_t* buffer, size_t p_size) = 0;

	/**
	 * Flush write stream to disk
	 * @return		true if the data could be flushed, otherwise false.
	 */
	virtual bool flush() = 0;

	/**
	 * Skip bytes in stream, forward or reverse
	 * @param		p_size		the number of bytes to skip.
	 *
	 * @return		true if successful, otherwise false.
	 */
	virtual bool skip(int64_t p_size) = 0;

	/**
	 * Tell byte offset in stream (similar to ftell).
	 * @return		current position of the stream.
	 */
	virtual uint64_t tell(void) = 0;

	/**
	 * Get number of bytes left before end of the stream
	 * @return		Number of bytes left.
	 */
	virtual uint64_t numBytesLeft(void) = 0;

	/**
	 * Seek to absolute offset in stream.
	 * @param		offset		absolute offset in stream

	 * @return		true if successful, otherwise false.
	 */
	virtual bool seek(uint64_t offset) = 0;

	/**
	 * Check if stream is seekable. (A stdin/stdout stream
	 * is not seekable).
	 *
	 * @return	 true if stream is seekable, otherwise false
	 */
	virtual bool hasSeek() = 0;
};

} // namespace grk
