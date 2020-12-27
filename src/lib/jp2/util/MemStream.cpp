/**
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
#include "grk_includes.h"

namespace grk {

MemStream::MemStream(uint8_t *buffer, size_t offset, size_t length, bool owns) :	buf(buffer),
																	off(offset),
																	len(length),
																	fd(0),
																	ownsBuffer(owns)
{}
MemStream::MemStream() : MemStream(nullptr, 0, 0, false)
{}
MemStream::~MemStream() {
	if (ownsBuffer)
		delete[] buf;
}

static void free_mem(void *user_data) {
	auto data = (MemStream*) user_data;
	if (data)
		delete data;
}

static size_t zero_copy_read_from_mem(void **p_buffer, size_t nb_bytes,
		MemStream *p_source_buffer) {
	size_t nb_read = 0;

	if (((size_t) p_source_buffer->off + nb_bytes) < p_source_buffer->len)
		nb_read = nb_bytes;

	*p_buffer = p_source_buffer->buf + p_source_buffer->off;
	assert(p_source_buffer->off + nb_read <= p_source_buffer->len);
	p_source_buffer->off += nb_read;

	return nb_read;
}

static size_t read_from_mem(void *p_buffer, size_t nb_bytes,
		MemStream *p_source_buffer) {
	size_t nb_read;

	if (!p_buffer)
		return 0;

	if (p_source_buffer->off + nb_bytes < p_source_buffer->len)
		nb_read = nb_bytes;
	else
		nb_read = (size_t) (p_source_buffer->len - p_source_buffer->off);

	if (nb_read) {
		assert(p_source_buffer->off + nb_read <= p_source_buffer->len);
		// (don't copy buffer into itself)
		if (p_buffer != p_source_buffer->buf + p_source_buffer->off)
			memcpy(p_buffer, p_source_buffer->buf + p_source_buffer->off,
					nb_read);
		p_source_buffer->off += nb_read;
	}

	return nb_read;
}

static size_t write_to_mem(void *dest, size_t nb_bytes, MemStream *src) {
	if (src->off + nb_bytes >= src->len)
		return 0;

	if (nb_bytes) {
		memcpy(src->buf + (size_t) src->off, dest, nb_bytes);
		src->off += nb_bytes;
	}
	return nb_bytes;
}

static bool seek_from_mem(uint64_t nb_bytes, MemStream *src) {
	if (nb_bytes < src->len)
		src->off = nb_bytes;
	else
		src->off = src->len;

	return true;
}

/**
 * Set the given function to be used as a zero copy read function.
 * NOTE: this feature is only available for memory mapped and buffer backed streams,
 * not file streams
 *
 * @param		stream	stream to modify
 * @param		p_function	function to use as read function.
 */
static void grk_stream_set_zero_copy_read_function(grk_stream *stream,
		grk_stream_zero_copy_read_fn p_function) {
	auto streamImpl = (grk::BufferedStream*) stream;
	if ((!streamImpl) || (!(streamImpl->m_status & GROK_STREAM_STATUS_INPUT)))
		return;
	streamImpl->m_zero_copy_read_fn = p_function;
}

void set_up_mem_stream(grk_stream *l_stream, size_t len, bool is_read_stream) {
	grk_stream_set_user_data_length(l_stream, len);
	if (is_read_stream) {
		grk_stream_set_read_function(l_stream,
				(grk_stream_read_fn) read_from_mem);
		grk_stream_set_zero_copy_read_function(l_stream,
				(grk_stream_zero_copy_read_fn) zero_copy_read_from_mem);
	} else
		grk_stream_set_write_function(l_stream,
				(grk_stream_write_fn) write_to_mem);
	grk_stream_set_seek_function(l_stream, (grk_stream_seek_fn) seek_from_mem);
}

size_t get_mem_stream_offset(grk_stream *stream) {
	if (!stream)
		return 0;
	auto bufferedStream = (BufferedStream*) stream;
	if (!bufferedStream->m_user_data)
		return 0;
	auto buf = (MemStream*) bufferedStream->m_user_data;

	return buf->off;
}

grk_stream* create_mem_stream(uint8_t *buf, size_t len, bool ownsBuffer,
		bool is_read_stream) {
	if (!buf || !len) {
		return nullptr;
	}
	auto memStream = new MemStream(buf, 0, len, ownsBuffer);
	auto l_stream = new BufferedStream(buf, len, is_read_stream);
	grk_stream_set_user_data((grk_stream*) l_stream, memStream, free_mem);
	set_up_mem_stream((grk_stream*) l_stream, memStream->len,
			is_read_stream);

	return (grk_stream*) l_stream;
}

}
