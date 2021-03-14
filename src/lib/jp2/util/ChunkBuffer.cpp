/*
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

/* #define DEBUG_CHUNK_BUF */

ChunkBuffer::ChunkBuffer() :
		data_len(0), cur_chunk_id(0) {
}

ChunkBuffer::~ChunkBuffer() {
	cleanup();
}

void ChunkBuffer::increment() {
	if (chunks.size() == 0 || cur_chunk_id == (size_t) (chunks.size() - 1))
		return;

	auto cur_chunk = chunks[cur_chunk_id];
	if (cur_chunk->offset == cur_chunk->len
			&& cur_chunk_id < (size_t) (chunks.size() - 1)) {
		cur_chunk_id++;
	}
}

size_t ChunkBuffer::read(void *p_buffer, size_t nb_bytes) {
	if (p_buffer == nullptr || nb_bytes == 0)
		return 0;

	/*don't try to read more bytes than are available */
	size_t bytes_remaining_in_file = data_len - (size_t) get_global_offset();
	if (nb_bytes > bytes_remaining_in_file) {
#ifdef DEBUG_CHUNK_BUF
        GRK_WARN("attempt to read past end of chunk buffer");
#endif
		nb_bytes = bytes_remaining_in_file;
	}

	size_t total_bytes_read = 0;
	size_t bytes_left_to_read = nb_bytes;
	while (bytes_left_to_read > 0 && cur_chunk_id < chunks.size()) {
		auto cur_chunk = chunks[cur_chunk_id];
		size_t bytes_in_current_chunk = (cur_chunk->len
				- (size_t) cur_chunk->offset);

		size_t bytes_to_read =
				(bytes_left_to_read < bytes_in_current_chunk) ?
						bytes_left_to_read : bytes_in_current_chunk;

		if (p_buffer) {
			memcpy((uint8_t*) p_buffer + total_bytes_read,
					cur_chunk->buf + cur_chunk->offset, bytes_to_read);
		}
		incr_cur_chunk_offset(bytes_to_read);
		total_bytes_read += bytes_to_read;
		bytes_left_to_read -= bytes_to_read;
	}

	return total_bytes_read;
}

size_t ChunkBuffer::skip(size_t nb_bytes) {
	size_t bytes_remaining;

	if (nb_bytes + get_global_offset() > data_len) {
#ifdef DEBUG_CHUNK_BUF
        GRK_WARN("attempt to skip past end of chunk buffer");
#endif
		return nb_bytes;
	}

	if (nb_bytes == 0)
		return 0;

	bytes_remaining = nb_bytes;
	while (cur_chunk_id < chunks.size() && bytes_remaining > 0) {

		grkBufferU8 *cur_chunk = chunks[cur_chunk_id];
		size_t bytes_in_current_chunk = (size_t) (cur_chunk->len - cur_chunk->offset);

		/* hoover up all the bytes in this chunk, and move to the next one */
		if (bytes_in_current_chunk > bytes_remaining) {

			incr_cur_chunk_offset(bytes_in_current_chunk);

			bytes_remaining -= bytes_in_current_chunk;
			cur_chunk = chunks[cur_chunk_id];
		} else { /* bingo! we found the chunk */
			incr_cur_chunk_offset(bytes_remaining);
			return nb_bytes;
		}
	}

	return nb_bytes;
}

grkBufferU8* ChunkBuffer::push_back(uint8_t *buf, size_t len, bool ownsData) {
	auto new_chunk = new grkBufferU8(buf, len, ownsData);
	push_back(new_chunk);

	return new_chunk;
}

void ChunkBuffer::push_back(grkBufferU8 *chunk) {
	if (!chunk)
		return;
	chunks.push_back(chunk);
	cur_chunk_id = (size_t) (chunks.size() - 1);
	data_len += chunk->len;
}

void ChunkBuffer::cleanup(void) {
	for (size_t i = 0; i < chunks.size(); ++i)
		delete chunks[i];
	chunks.clear();
}

void ChunkBuffer::rewind(void) {
	for (size_t i = 0; i < chunks.size(); ++i) {
		grkBufferU8 *chunk = chunks[i];
		if (chunk)
			chunk->offset = 0;
	}
	cur_chunk_id = 0;
}

bool ChunkBuffer::alloc_and_push_back(size_t len) {
	if (!len)
		return false;
	auto buf = new uint8_t[len];
	auto chunk = push_back(buf, len, true);
	if (!chunk) {
		delete[] buf;
		return false;
	}

	return true;
}

void ChunkBuffer::incr_cur_chunk_offset(size_t offset) {
	auto cur_chunk = chunks[cur_chunk_id];

	cur_chunk->incrementOffset((ptrdiff_t) offset);
	if (cur_chunk->offset == cur_chunk->len)
		increment();
}

/**
 * Zero copy read of contiguous chunk from current chunk.
 * Returns false if unable to get a contiguous chunk, true otherwise
 */
bool ChunkBuffer::zero_copy_read(uint8_t **ptr, size_t chunk_len) {
	auto cur_chunk = chunks[cur_chunk_id];

	if (!cur_chunk)
		return false;

	if ((size_t) cur_chunk->offset + chunk_len <= cur_chunk->len) {
		*ptr = cur_chunk->buf + cur_chunk->offset;
		return (read(nullptr, chunk_len) == chunk_len);
	}

	return false;
}

bool ChunkBuffer::copyToContiguousBuffer(uint8_t *buffer) {
	size_t offset = 0;

	if (!buffer)
		return false;

	for (size_t i = 0; i < chunks.size(); ++i) {
		auto chunk = chunks[i];
		if (chunk->len)
			memcpy(buffer + offset, chunk->buf, chunk->len);
		offset += chunk->len;
	}
	return true;

}

uint8_t* ChunkBuffer::get_cur_chunk_ptr(void) {
	auto cur_chunk = chunks[cur_chunk_id];

	return (cur_chunk) ? cur_chunk->currPtr() : nullptr;
}

size_t ChunkBuffer::get_cur_chunk_len(void) {
	auto cur_chunk = chunks[cur_chunk_id];

	return (cur_chunk) ? cur_chunk->remainingLength() : 0;
}

size_t ChunkBuffer::get_cur_chunk_offset(void) {
	auto cur_chunk = chunks[cur_chunk_id];

	return (cur_chunk) ? cur_chunk->offset : 0;
}

size_t ChunkBuffer::get_global_offset(void) {
	size_t offset = 0;

	for (size_t i = 0; i < cur_chunk_id; ++i) {
		grkBufferU8 *chunk = chunks[i];
		offset += chunk->len;
	}

	return offset + get_cur_chunk_offset();
}

}
