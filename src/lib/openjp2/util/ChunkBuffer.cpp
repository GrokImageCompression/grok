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
 */
#include "grok_includes.h"

namespace grk {

/* #define DEBUG_CHUNK_BUF */

ChunkBuffer::ChunkBuffer() :
		data_len(0), cur_chunk_id(0) {
}

ChunkBuffer::~ChunkBuffer() {
	cleanup();
}

void ChunkBuffer::increment() {
	grk_buf *cur_chunk = nullptr;
	if (cur_chunk_id == chunks.size() - 1) {
		return;
	}

	cur_chunk = chunks[cur_chunk_id];
	if ((size_t) cur_chunk->offset == cur_chunk->len
			&& cur_chunk_id < chunks.size() - 1) {
		cur_chunk_id++;
	}
}

size_t ChunkBuffer::read(void *p_buffer, size_t nb_bytes) {
	size_t bytes_in_current_segment;
	size_t bytes_to_read;
	size_t total_bytes_read;
	size_t bytes_left_to_read;
	size_t bytes_remaining_in_file;

	if (p_buffer == nullptr || nb_bytes == 0)
		return 0;

	/*don't try to read more bytes than are available */
	bytes_remaining_in_file = data_len - (size_t) get_global_offset();
	if (nb_bytes > bytes_remaining_in_file) {
#ifdef DEBUG_CHUNK_BUF
        GROK_WARN("attempt to read past end of chunked buffer");
#endif
		nb_bytes = bytes_remaining_in_file;
	}

	total_bytes_read = 0;
	bytes_left_to_read = nb_bytes;
	while (bytes_left_to_read > 0 && cur_chunk_id < chunks.size()) {
		grk_buf *cur_chunk = chunks[cur_chunk_id];
		bytes_in_current_segment = (cur_chunk->len - (size_t) cur_chunk->offset);

		bytes_to_read =
				(bytes_left_to_read < bytes_in_current_segment) ?
						bytes_left_to_read : bytes_in_current_segment;

		if (p_buffer) {
			memcpy((uint8_t*) p_buffer + total_bytes_read,
					cur_chunk->buf + cur_chunk->offset, bytes_to_read);
		}
		incr_cur_chunk_offset((int64_t) bytes_to_read);
		total_bytes_read += bytes_to_read;
		bytes_left_to_read -= bytes_to_read;
	}
	return total_bytes_read ? total_bytes_read : (size_t) -1;
}


int64_t ChunkBuffer::skip(int64_t nb_bytes)
{
    size_t bytes_in_current_segment;
    size_t bytes_remaining;

    if (nb_bytes + get_global_offset()> (int64_t)data_len) {
#ifdef DEBUG_SEG_BUF
        GROK_WARN("attempt to skip past end of segmented buffer");
#endif
        return nb_bytes;
    }

    if (nb_bytes == 0)
        return 0;

    bytes_remaining = (size_t)nb_bytes;
    while (cur_chunk_id < chunks.size() && bytes_remaining > 0) {

		grk_buf *cur_chunk = chunks[cur_chunk_id];
        bytes_in_current_segment = 	(size_t)(cur_chunk->len -cur_chunk->offset);

        /* hoover up all the bytes in this chunk, and move to the next one */
        if (bytes_in_current_segment > bytes_remaining) {

            incr_cur_chunk_offset(bytes_in_current_segment);

            bytes_remaining	-= bytes_in_current_segment;
            cur_chunk = chunks[cur_chunk_id];
        } else { /* bingo! we found the chunk */
            incr_cur_chunk_offset(bytes_remaining);
            return nb_bytes;
        }
    }
    return nb_bytes;
}

grk_buf* ChunkBuffer::add_chunk(uint8_t *buf, size_t len, bool ownsData) {
	auto new_seg = new grk_buf(buf, len, ownsData);
	add_chunk(new_seg);
	return new_seg;
}

void ChunkBuffer::add_chunk(grk_buf *chunk) {
	if (!chunk)
		return;
	chunks.push_back(chunk);
	cur_chunk_id = (int32_t) chunks.size() - 1;
	data_len += chunk->len;
}

void ChunkBuffer::cleanup(void) {
	size_t i;
	for (i = 0; i < chunks.size(); ++i) {
		grk_buf *chunk = chunks[i];
		if (chunk) {
			delete chunk;
		}
	}
	chunks.clear();
}

void ChunkBuffer::rewind(void) {
	size_t i;
	for (i = 0; i < chunks.size(); ++i) {
		grk_buf *chunk = chunks[i];
		if (chunk) {
			chunk->offset = 0;
		}
	}
	cur_chunk_id = 0;
}
bool ChunkBuffer::push_back(uint8_t *buf, size_t len) {
	if (!buf || !len) {
		return false;
	}
	auto chunk = add_chunk(buf, len, false);
	if (!chunk)
		return false;
	return true;
}

bool ChunkBuffer::alloc_and_push_back(size_t len) {
	grk_buf *chunk = nullptr;
	uint8_t *buf = nullptr;
	if (!len)
		return false;
	buf = new uint8_t[len];
	chunk = add_chunk(buf, len, true);
	if (!chunk) {
		delete[] buf;
		return false;
	}
	return true;
}

void ChunkBuffer::incr_cur_chunk_offset(uint64_t offset) {
	grk_buf *cur_chunk = nullptr;
	cur_chunk = chunks[cur_chunk_id];
	cur_chunk->incr_offset(offset);
	if ((size_t) cur_chunk->offset == cur_chunk->len) {
		increment();
	}

}

/**
 * Zero copy read of contiguous chunk from current segment.
 * Returns false if unable to get a contiguous chunk, true otherwise
 */
bool ChunkBuffer::zero_copy_read(uint8_t **ptr, size_t chunk_len) {
	grk_buf *cur_chunk = nullptr;
	cur_chunk = chunks[cur_chunk_id];
	if (!cur_chunk)
		return false;

	if ((size_t) cur_chunk->offset + chunk_len <= cur_chunk->len) {
		*ptr = cur_chunk->buf + cur_chunk->offset;
		read(nullptr, chunk_len);
		return true;
	}
	return false;
}

bool ChunkBuffer::copy_to_contiguous_buffer(uint8_t *buffer) {
	size_t i = 0;
	size_t offset = 0;

	if (!buffer)
		return false;

	for (i = 0; i < chunks.size(); ++i) {
		grk_buf *chunk = chunks[i];
		if (chunk->len)
			memcpy(buffer + offset, chunk->buf, chunk->len);
		offset += chunk->len;
	}
	return true;

}

uint8_t* ChunkBuffer::get_global_ptr(void) {
	grk_buf *cur_chunk = nullptr;
	cur_chunk = chunks[cur_chunk_id];
	return (cur_chunk) ? (cur_chunk->buf + cur_chunk->offset) : nullptr;
}

size_t ChunkBuffer::get_cur_chunk_len(void) {
	grk_buf *cur_chunk = nullptr;
	cur_chunk = chunks[cur_chunk_id];
	return (cur_chunk) ? (cur_chunk->len - (size_t) cur_chunk->offset) : 0;
}

int64_t ChunkBuffer::get_cur_chunk_offset(void) {
	grk_buf *cur_chunk = nullptr;
	cur_chunk = chunks[cur_chunk_id];
	return (cur_chunk) ? (int64_t) (cur_chunk->offset) : 0;
}

int64_t ChunkBuffer::get_global_offset(void) {
	int64_t offset = 0;
	for (size_t i = 0; i < cur_chunk_id; ++i) {
		grk_buf *chunk = chunks[i];
		offset += (int64_t) chunk->len;
	}
	return offset + get_cur_chunk_offset();
}

}
