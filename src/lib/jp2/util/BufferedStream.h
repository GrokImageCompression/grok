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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

#include <IBufferedStream.h>
#include "grk_config_private.h"
#include "IBitIO.h"

namespace grk {

#define GROK_STREAM_STATUS_OUTPUT  0x1U
#define GROK_STREAM_STATUS_INPUT   0x2U
#define GROK_STREAM_STATUS_END     0x4U
#define GROK_STREAM_STATUS_ERROR   0x8U

/**
 Byte input-output stream.
 */
struct BufferedStream: public IBufferedStream {

	BufferedStream(uint8_t *buffer, size_t buffer_size, bool l_is_input);
	~BufferedStream();

	/**
	 * user data
	 */
	void *m_user_data;

	/**
	 * Pointer to function to free m_user_data (nullptr at initialization)
	 * when destroying the stream. If pointer is nullptr the function is not
	 * called and the m_user_data is not freed (even if it isn't nullptr).
	 */
	grk_stream_free_user_data_fn m_free_user_data_fn;

	/**
	 * User data length.
	 * Currently set to size of file for file read stream,
	 * and size of buffer for buffer read/write stream
	 */
	uint64_t m_user_data_length;

	/**
	 * Pointer to actual read function (nullptr at initialization).
	 */
	grk_stream_read_fn m_read_fn;

	/**
	 * Pointer to actual zero copy read function (nullptr at initialization).
	 */
	grk_stream_zero_copy_read_fn m_zero_copy_read_fn;

	/**
	 * Pointer to actual write function (nullptr at initialization).
	 */
	grk_stream_write_fn m_write_fn;

	/**
	 * Pointer to actual seek function (if available).
	 */
	grk_stream_seek_fn m_seek_fn;

	/**
	 * Stream status flags
	 */
	uint32_t m_status;

	/**
	 * Reads some bytes from the stream.
	 * @param		p_buffer	pointer to the data buffer
	 * 							that will receive the data.
	 * @param		p_size		number of bytes to read.
	 
	 * @return		the number of bytes read
	 */
	size_t read(uint8_t *p_buffer, size_t p_size);

	size_t read_data_zero_copy(uint8_t **p_buffer, size_t p_size);

	bool write_byte(uint8_t value);

	// low-level write methods that take endian into account
	bool write_short(uint16_t value);
	bool write_24(uint32_t value);
	bool write_int(uint32_t value);
	bool write_64(uint64_t value);

	/**
	 * Write bytes to stream (no correction for endian!).
	 * @param		p_buffer	pointer to the data buffer holds the data
	 * 							to be written.
	 * @param		p_size		number of bytes to write.
	 
	 * @return		the number of bytes written
	 */
	size_t write_bytes(const uint8_t *p_buffer, size_t p_size);

	/**
	 * Flush stream to disk
	 
	 * @return		true if the data could be flushed, otherwise false.
	 */
	bool flush();

	/**
	 * Skip bytes in stream.
	 * @param		p_size		the number of bytes to skip.
	 
	 * @return		true if success, otherwise false
	 */
	bool skip(int64_t p_size);

	/**
	 * Tells byte offset of stream (similar to ftell).
	 *
	 * @return		the current position of the stream.
	 */
	uint64_t tell(void);

	/**
	 * Get the number of bytes left before end of stream
	 *
	 * @return		Number of bytes left before the end of the stream.
	 */
	uint64_t get_number_byte_left(void);

	/**
	 * Seek bytes from the stream (absolute)
	 * @param		offset		the number of bytes to skip.
	 *
	 * @return		true if successful, otherwise false
	 */
	bool seek(uint64_t offset);

	/**
	 * Check if stream is seekable.
	 */
	bool has_seek();

	bool supportsZeroCopy() ;
	uint8_t* getCurrentPtr();

private:

	/**
	 * Skip bytes in write stream.
	 * @param		p_size		the number of bytes to skip.
	 
	 * @return		true if success, otherwise false
	 */
	bool write_skip(int64_t p_size);

	/**
	 * Skip bytes in read stream.
	 * @param		p_size		the number of bytes to skip.
	 
	 * @return		true if success, otherwise false
	 */
	bool read_skip(int64_t p_size);

	/**
	 * Absolute seek in read stream.
	 * @param		offset		absolute offset
	 
	 * @return		true if success, otherwise false
	 */
	bool read_seek(uint64_t offset);

	/**
	 * Absolute seek in write stream.
	 * @param		offset		absolute offset
	 * @return		true if success, otherwise false
	 */
	bool write_seek(uint64_t offset);

	void write_increment(size_t p_size);
	template<typename TYPE> bool write(TYPE value, uint8_t numBytes);
	void invalidate_buffer();

	bool isMemStream();

	grk_buf *m_buf;

	// number of bytes read in, or slated for write
	size_t m_buffered_bytes;

	// number of seekable bytes in buffer. This will equal
	// the number of bytes
	// read in the last media read.
	// We always have m_buffered_bytes <= m_read_bytes_seekable
	size_t m_read_bytes_seekable;

	// number of bytes read/written from the beginning of the stream
	uint64_t m_stream_offset;

};

template<typename TYPE> void grk_write(uint8_t *p_buffer, TYPE value,
		uint32_t nb_bytes) {
	if (nb_bytes == 0)
		return;
	assert(nb_bytes <= sizeof(TYPE));
#if defined(GROK_BIG_ENDIAN)
	const uint8_t * l_data_ptr = ((const uint8_t *)&value) + sizeof(TYPE) - nb_bytes;
	memcpy(p_buffer, l_data_ptr, nb_bytes);
#else
	const uint8_t *l_data_ptr = ((const uint8_t*) &value)
			+ (size_t)(nb_bytes - 1);
	for (uint32_t i = 0; i < nb_bytes; ++i) {
		*(p_buffer++) = *(l_data_ptr--);
	}
#endif
}

template<typename TYPE> void grk_write(uint8_t *p_buffer, TYPE value) {
	grk_write<TYPE>(p_buffer, value, sizeof(TYPE));
}

template<typename TYPE> void grk_read(const uint8_t *p_buffer, TYPE *value,
		uint32_t nb_bytes) {
	assert(nb_bytes > 0 && nb_bytes <= sizeof(TYPE));
#if defined(GROK_BIG_ENDIAN)
	auto l_data_ptr = ((uint8_t *)value);
	*value = 0;
	memcpy(l_data_ptr + sizeof(TYPE) - nb_bytes, p_buffer, nb_bytes);
#else
	auto l_data_ptr = ((uint8_t*) value) + nb_bytes - 1;
	*value = 0;
	for (uint32_t i = 0; i < nb_bytes; ++i)
		*(l_data_ptr--) = *(p_buffer++);
#endif
}

template<typename TYPE> void grk_read(const uint8_t *p_buffer, TYPE *value){
	grk_read<TYPE>(p_buffer, value, sizeof(TYPE));
}

}
