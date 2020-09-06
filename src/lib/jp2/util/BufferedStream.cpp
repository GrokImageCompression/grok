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
#include "grk_includes.h"
namespace grk {

template<typename TYPE> void grk_write(uint8_t *p_buffer, TYPE value,
		uint32_t nb_bytes);
template<typename TYPE> void grk_read(const uint8_t *p_buffer, TYPE *value,
		uint32_t nb_bytes);

// buffered stream
BufferedStream::BufferedStream(uint8_t *buffer, size_t buffer_size,
		bool is_input) :
		m_user_data(nullptr), m_free_user_data_fn(nullptr), m_user_data_length(
				0), m_read_fn(nullptr), m_zero_copy_read_fn(nullptr), m_write_fn(
				nullptr), m_seek_fn(nullptr), m_status(
				is_input ?
				GROK_STREAM_STATUS_INPUT :
								GROK_STREAM_STATUS_OUTPUT), m_buf(nullptr), m_buffered_bytes(
				0), m_read_bytes_seekable(0), m_stream_offset(0) {

	m_buf = new grk_buf(
			(!buffer && buffer_size) ? new uint8_t[buffer_size] : buffer,
			buffer_size, buffer == nullptr);
}

BufferedStream::~BufferedStream() {
	if (m_free_user_data_fn)
		m_free_user_data_fn(m_user_data);
	delete m_buf;
}
//note: passing in nullptr for p_buffer will execute a zero-copy read
size_t BufferedStream::read(uint8_t *p_buffer, size_t p_size) {

	if (!p_buffer && !supportsZeroCopy())
		throw new std::exception();
	assert(p_size);
	if (!p_size)
		return 0;
	size_t read_nb_bytes = 0;

	//1. if stream is at end, then return immediately
	if (m_status & GROK_STREAM_STATUS_END)
		return 0;
	//2. if we have enough bytes in buffer, then read from buffer and return
	if (p_size <= m_buffered_bytes) {
		if (p_buffer) {
			assert(m_buf->curr_ptr() >= m_buf->buf);
			assert(
					(ptrdiff_t )m_buf->curr_ptr() - (ptrdiff_t )m_buf->buf
							+ (ptrdiff_t )p_size <= (ptrdiff_t )m_buf->len);
			memcpy(p_buffer, m_buf->curr_ptr(), p_size);
		}
		m_buf->incr_offset((ptrdiff_t) p_size);
		m_buffered_bytes -= p_size;
		assert(m_buffered_bytes <= m_read_bytes_seekable);
		read_nb_bytes += p_size;
		m_stream_offset += p_size;
		return read_nb_bytes;
	}
	//3. if stream is at end, then read remaining bytes in buffer and return
	if (m_status & GROK_STREAM_STATUS_END) {
		read_nb_bytes += m_buffered_bytes;
		if (p_buffer && m_buffered_bytes) {
			assert(m_buf->curr_ptr() >= m_buf->buf);
			assert(
					(ptrdiff_t )m_buf->curr_ptr() - (ptrdiff_t )m_buf->buf
							+ (ptrdiff_t )m_buffered_bytes
							<= (ptrdiff_t )m_buf->len);
			memcpy(p_buffer, m_buf->curr_ptr(), m_buffered_bytes);
		}
		m_stream_offset += m_buffered_bytes;
		invalidate_buffer();
		return read_nb_bytes;
	}
	// 4. read remaining bytes in buffer
	if (m_buffered_bytes) {
		read_nb_bytes += m_buffered_bytes;
		if (p_buffer) {
			assert(m_buf->curr_ptr() >= m_buf->buf);
			assert(
					(ptrdiff_t )m_buf->curr_ptr() - (ptrdiff_t )m_buf->buf
							+ (ptrdiff_t )m_buffered_bytes
							<= (ptrdiff_t )m_buf->len);
			memcpy(p_buffer, m_buf->curr_ptr(), m_buffered_bytes);
			p_buffer += m_buffered_bytes;
		}
		p_size -= m_buffered_bytes;
		m_stream_offset += m_buffered_bytes;
		m_buffered_bytes = 0;
	}

	//5. read from "media"
	invalidate_buffer();
	while (true) {
		m_buffered_bytes = m_read_fn(m_buf->curr_ptr(), m_buf->len,
				m_user_data);
		//sanity check on external read function
		if (m_buffered_bytes > m_buf->len) {
			GRK_ERROR(
					"Buffered stream: read length greater than buffer length");
			return 0;
		}
		m_read_bytes_seekable = m_buffered_bytes;
		// i) end of stream
		if (m_buffered_bytes == 0 || m_buffered_bytes > m_buf->len) {
			invalidate_buffer();
			m_status |= GROK_STREAM_STATUS_END;
			return read_nb_bytes;
		}
		// ii) or not enough data
		else if (m_buffered_bytes < p_size) {
			read_nb_bytes += m_buffered_bytes;
			if (p_buffer) {
				assert(m_buf->curr_ptr() >= m_buf->buf);
				assert(
						(ptrdiff_t )m_buf->curr_ptr() - (ptrdiff_t )m_buf->buf
								+ (ptrdiff_t )m_buffered_bytes
								<= (ptrdiff_t )m_buf->len);
				memcpy(p_buffer, m_buf->curr_ptr(), m_buffered_bytes);
				p_buffer += m_buffered_bytes;
			}
			p_size -= m_buffered_bytes;
			m_stream_offset += m_buffered_bytes;
			invalidate_buffer();
		}
		// iii) or we have read the exact amount requested
		else {
			read_nb_bytes += p_size;
			if (p_buffer && p_size) {
				assert(m_buf->curr_ptr() >= m_buf->buf);
				assert(
						(ptrdiff_t )m_buf->curr_ptr() - (ptrdiff_t )m_buf->buf
								+ (ptrdiff_t )p_size <= (ptrdiff_t )m_buf->len);
				memcpy(p_buffer, m_buf->curr_ptr(), p_size);
			}
			m_buf->incr_offset((ptrdiff_t) p_size);
			m_buffered_bytes -= p_size;
			assert(m_buffered_bytes <= m_read_bytes_seekable);
			m_stream_offset += p_size;
			return read_nb_bytes;
		}
	}
	return 0;
}
size_t BufferedStream::read_data_zero_copy(uint8_t **p_buffer, size_t p_size) {

	size_t read_nb_bytes = m_zero_copy_read_fn((void**) p_buffer, p_size,
			m_user_data);

	if (read_nb_bytes == 0) {
		m_status |= GROK_STREAM_STATUS_END;
		return 0;
	} else {
		m_stream_offset += read_nb_bytes;
		return read_nb_bytes;
	}
}
bool BufferedStream::write_byte(uint8_t value) {
	return write_bytes(&value, 1) == 1;
}
bool BufferedStream::write_short(uint16_t value) {
	return write<uint16_t>(value, sizeof(uint16_t));
}
bool BufferedStream::write_24(uint32_t value) {
	return write<uint32_t>(value, 3);
}
bool BufferedStream::write_int(uint32_t value) {
	return write<uint32_t>(value, sizeof(uint32_t));
}
bool BufferedStream::write_64(uint64_t value) {
	return write<uint64_t>(value, sizeof(uint64_t));
}
template<typename TYPE> bool BufferedStream::write(TYPE value,
		uint8_t numBytes) {
	if (m_status & GROK_STREAM_STATUS_ERROR)
		return false;
	if (numBytes > sizeof(TYPE))
		return false;

	// handle case where there is no internal buffer (buffer stream)
	if (isMemStream()) {
		// skip first to make sure that we are not at the end of the stream
		if (!m_seek_fn(m_stream_offset + numBytes, m_user_data))
			return false;
		grk_write(m_buf->curr_ptr(), value, numBytes);
		write_increment(numBytes);
		return true;
	}
	size_t remaining_bytes = m_buf->len - m_buffered_bytes;
	if (remaining_bytes < numBytes) {
		if (!flush())
			return false;
	}
	grk_write(m_buf->curr_ptr(), value, numBytes);
	write_increment(numBytes);
	return true;
}
size_t BufferedStream::write_bytes(const uint8_t *p_buffer, size_t p_size) {
	assert(p_size);
	if (!p_size || !p_buffer)
		return 0;

	if (m_status & GROK_STREAM_STATUS_ERROR)
		return 0;

	// handle case where there is no internal buffer (buffer stream)
	if (isMemStream()) {
		/* we should do an actual write on the media */
		auto current_write_nb_bytes = m_write_fn((uint8_t*) p_buffer, p_size,
				m_user_data);
		write_increment(current_write_nb_bytes);

		return current_write_nb_bytes;
	}
	size_t write_nb_bytes = 0;
	while (true) {
		size_t remaining_bytes = m_buf->len - m_buffered_bytes;

		/* we have more memory than required */
		if (remaining_bytes >= p_size) {
			write_nb_bytes += p_size;
			memcpy(m_buf->curr_ptr(), p_buffer, p_size);
			write_increment(p_size);
			return write_nb_bytes;
		}

		/* we copy part of data (if possible) and flush the stream */
		if (remaining_bytes) {
			write_nb_bytes += remaining_bytes;
			memcpy(m_buf->curr_ptr(), p_buffer, remaining_bytes);
			m_buf->offset = 0;
			m_buffered_bytes += remaining_bytes;
			m_stream_offset += remaining_bytes;
			p_buffer += remaining_bytes;
			p_size -= remaining_bytes;
		}
		if (!flush())
			return 0;
	}

	return write_nb_bytes;
}
void BufferedStream::write_increment(size_t p_size) {
	m_buf->incr_offset((ptrdiff_t) p_size);
	if (!isMemStream())
		m_buffered_bytes += p_size;
	else
		assert(m_buffered_bytes == 0);
	m_stream_offset += p_size;
}

// force write of any remaining bytes from double buffer
bool BufferedStream::flush() {
	if (isMemStream())
		return true;
	/* the number of bytes written on the media. */
	m_buf->offset = 0;
	while (m_buffered_bytes) {
		/* we should do an actual write on the media */
		size_t current_write_nb_bytes = m_write_fn(m_buf->curr_ptr(),
				m_buffered_bytes, m_user_data);

		if (current_write_nb_bytes != m_buffered_bytes) {
			m_status |= GROK_STREAM_STATUS_ERROR;
			GRK_ERROR("Error on writing stream.");
			return false;
		}
		m_buf->incr_offset((ptrdiff_t) current_write_nb_bytes);
		assert(m_buf->curr_ptr() >= m_buf->buf);
		m_buffered_bytes -= current_write_nb_bytes;
		assert(m_buffered_bytes <= m_read_bytes_seekable);
	}
	m_buf->offset = 0;

	return true;
}

void BufferedStream::invalidate_buffer() {
	m_buf->offset = 0;
	m_buffered_bytes = 0;
	if (m_status & GROK_STREAM_STATUS_INPUT)
		m_read_bytes_seekable = 0;
}
bool BufferedStream::supportsZeroCopy() {
	return isMemStream() && (m_status & GROK_STREAM_STATUS_INPUT);
}
uint8_t* BufferedStream::getCurrentPtr() {
	return m_buf->curr_ptr();
}

bool BufferedStream::read_skip(int64_t p_size) {
	int64_t offset = (int64_t) m_stream_offset + p_size;

	if (offset < 0)
		return false;

	return read_seek((uint64_t) offset);
}

bool BufferedStream::write_skip(int64_t p_size) {
	int64_t offset = (int64_t) m_stream_offset + p_size;
	if (offset < 0)
		return false;
	return write_seek((uint64_t) offset);
}
uint64_t BufferedStream::tell() {
	return m_stream_offset;
}
uint64_t BufferedStream::get_number_byte_left(void) {
	assert(m_user_data_length >= m_stream_offset);
	return m_user_data_length ?
			(uint64_t) (m_user_data_length - m_stream_offset) : 0;
}
bool BufferedStream::skip(int64_t p_size) {
	if (m_status & GROK_STREAM_STATUS_INPUT)
		return read_skip(p_size);
	else
		return write_skip(p_size);
}
// absolute seek
bool BufferedStream::read_seek(uint64_t offset) {

	if (m_status & GROK_STREAM_STATUS_ERROR)
		return false;

	// 1. try to seek in buffer
	if (!(m_status & GROK_STREAM_STATUS_END)) {
		if ((offset >= m_stream_offset
				&& offset < m_stream_offset + m_buffered_bytes)
				|| (offset < m_stream_offset
						&& offset
								>= m_stream_offset
										- (m_read_bytes_seekable
												- m_buffered_bytes))) {
			int64_t increment = (int64_t) offset - (int64_t) m_stream_offset;
			m_stream_offset = offset;
			m_buf->incr_offset((ptrdiff_t) increment);
			assert(m_buf->curr_ptr() >= m_buf->buf);
			m_buffered_bytes =
					(size_t) ((int64_t) m_buffered_bytes - increment);
			assert(m_buffered_bytes <= m_read_bytes_seekable);

			return true;
		}
	}

	//2. Since we can't seek in buffer, we must invalidate
	//  buffer contents and seek in media
	invalidate_buffer();
	if (!(m_seek_fn(offset, m_user_data))) {
		m_status |= GROK_STREAM_STATUS_END;
		return false;
	} else {
		m_status &= (~GROK_STREAM_STATUS_END);
		m_stream_offset = offset;
	}
	return true;
}

//absolute seek in stream
bool BufferedStream::write_seek(uint64_t offset) {
	if (m_status & GROK_STREAM_STATUS_ERROR)
		return false;

	if (!flush()) {
		m_status |= GROK_STREAM_STATUS_ERROR;
		return false;
	}
	invalidate_buffer();
	if (!m_seek_fn(offset, m_user_data)) {
		m_status |= GROK_STREAM_STATUS_ERROR;
		return false;
	} else {
		m_stream_offset = offset;
	}
	if (isMemStream())
		m_buf->offset = offset;
	return true;
}
bool BufferedStream::seek(uint64_t offset) {
	if (m_status & GROK_STREAM_STATUS_INPUT)
		return read_seek(offset);
	else
		return write_seek(offset);
}
bool BufferedStream::has_seek(void) {
	return m_seek_fn != nullptr;
}

bool BufferedStream::isMemStream() {
	return !m_buf->owns_data;
}

}
grk_stream* grk_stream_create(size_t buffer_size,
		bool is_input) {
	return (grk_stream*) (new grk::BufferedStream(nullptr, buffer_size,
			is_input));
}
void grk_stream_destroy(grk_stream *stream) {
	delete (grk::BufferedStream*) (stream);
}
void grk_stream_set_read_function(grk_stream *stream,
		grk_stream_read_fn p_function) {
	auto streamImpl = (grk::BufferedStream*) stream;
	if ((!streamImpl) || (!(streamImpl->m_status & GROK_STREAM_STATUS_INPUT)))
		return;
	streamImpl->m_read_fn = p_function;
}

void grk_stream_set_seek_function(grk_stream *stream,
		grk_stream_seek_fn p_function) {
	auto streamImpl = (grk::BufferedStream*) stream;
	if (streamImpl)
		streamImpl->m_seek_fn = p_function;
}
void grk_stream_set_write_function(grk_stream *stream,
		grk_stream_write_fn p_function) {
	auto streamImpl = (grk::BufferedStream*) stream;
	if ((!streamImpl) || (!(streamImpl->m_status & GROK_STREAM_STATUS_OUTPUT)))
		return;

	streamImpl->m_write_fn = p_function;
}

void grk_stream_set_user_data(grk_stream *stream, void *p_data,
		grk_stream_free_user_data_fn p_function) {
	auto streamImpl = (grk::BufferedStream*) stream;
	if (!streamImpl)
		return;
	streamImpl->m_user_data = p_data;
	streamImpl->m_free_user_data_fn = p_function;
}
void grk_stream_set_user_data_length(grk_stream *stream,
		uint64_t data_length) {
	auto streamImpl = (grk::BufferedStream*) stream;
	if (streamImpl)
		streamImpl->m_user_data_length = data_length;
}
