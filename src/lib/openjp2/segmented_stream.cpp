/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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


/* #define DEBUG_SEG_BUF */


/*  Segmented Buffer Stream */
seg_buf_t::seg_buf_t() : data_len(0), cur_seg_id(0) {
}

seg_buf_t::~seg_buf_t()  {
	for (auto& seg : segments) {
		if (seg) {
			delete seg;
		}
	}
}

void seg_buf_t::increment()
{
    buf_t* cur_seg = nullptr;
    if (cur_seg_id == segments.size()-1) {
        return;
    }

    cur_seg = segments[cur_seg_id];
    if ((size_t)cur_seg->offset == cur_seg->len  &&
            cur_seg_id < segments.size() -1)	{
        cur_seg_id++;
    }
}

size_t seg_buf_t::read(void * p_buffer, size_t p_nb_bytes)
{
    size_t bytes_in_current_segment;
    size_t bytes_to_read;
    size_t total_bytes_read;
    size_t bytes_left_to_read;
    size_t bytes_remaining_in_file;

    if (p_buffer == nullptr || p_nb_bytes == 0 )
        return 0;

    /*don't try to read more bytes than are available */
    bytes_remaining_in_file = data_len - (size_t)get_global_offset();
    if (p_nb_bytes > bytes_remaining_in_file) {
#ifdef DEBUG_SEG_BUF
        printf("[WARNING]  attempt to read past end of segmented buffer\n");
#endif
        p_nb_bytes = bytes_remaining_in_file;
    }

    total_bytes_read = 0;
    bytes_left_to_read = p_nb_bytes;
    while (bytes_left_to_read > 0 && cur_seg_id < segments.size()) {
        buf_t* cur_seg = segments[cur_seg_id];
        bytes_in_current_segment = (cur_seg->len - (size_t)cur_seg->offset);

        bytes_to_read = (bytes_left_to_read < bytes_in_current_segment) ?
                        bytes_left_to_read : bytes_in_current_segment;

        if (p_buffer) {
            memcpy((uint8_t*)p_buffer + total_bytes_read,cur_seg->buf + cur_seg->offset, bytes_to_read);
        }
        incr_cur_seg_offset((int64_t)bytes_to_read);
        total_bytes_read	+= bytes_to_read;
        bytes_left_to_read	-= bytes_to_read;
    }
    return total_bytes_read ? total_bytes_read : (size_t)-1;
}

/* Disable this method for now, since it is not needed at the moment */
#if 0
int64_t seg_buf_t::skip(int64_t p_nb_bytes)
{
    size_t bytes_in_current_segment;
    size_t bytes_remaining;

    if (!seg_buf)
        return p_nb_bytes;

    if (p_nb_bytes + get_global_offset()> (int64_t)data_len) {
#ifdef DEBUG_SEG_BUF
        printf("[WARNING]  attempt to skip past end of segmented buffer\n");
#endif
        return p_nb_bytes;
    }

    if (p_nb_bytes == 0)
        return 0;

    bytes_remaining = (size_t)p_nb_bytes;
    while (cur_seg_id < segments.size && bytes_remaining > 0) {

        buf_t* cur_seg = (buf_t*)opj_vec_get(&segments, cur_seg_id);
        bytes_in_current_segment = 	(size_t)(cur_seg->len -cur_seg->offset);

        /* hoover up all the bytes in this segment, and move to the next one */
        if (bytes_in_current_segment > bytes_remaining) {

            incr_cur_seg_offset(seg_buf, bytes_in_current_segment);

            bytes_remaining	-= bytes_in_current_segment;
            cur_seg = (buf_t*)opj_vec_get(&segments, cur_seg_id);
        } else { /* bingo! we found the segment */
            incr_cur_seg_offset(seg_buf, bytes_remaining);
            return p_nb_bytes;
        }
    }
    return p_nb_bytes;
}
#endif
buf_t* seg_buf_t::add_segment(uint8_t* buf, size_t len, bool ownsData)
{
    auto new_seg = new buf_t(buf, len,ownsData);
	add_segment(new_seg);
    return new_seg;
}

void  seg_buf_t::add_segment(buf_t* seg) {
	if (!seg)
		return;
	segments.push_back(seg);
	cur_seg_id = (int32_t)segments.size() - 1;
	data_len += seg->len;
}

void seg_buf_t::cleanup(void)
{
    size_t i;
    for (i = 0; i < segments.size(); ++i) {
        buf_t* seg = segments[i];
        if (seg) {
            delete seg;
        }
    }
    segments.clear();
}

void seg_buf_t::rewind(void)
{
	size_t i;
    for (i = 0; i < segments.size(); ++i) {
        buf_t* seg = segments[i];
        if (seg) {
            seg->offset = 0;
        }
    }
    cur_seg_id = 0;
}
bool seg_buf_t::push_back(uint8_t* buf, size_t len)
{
    buf_t* seg = nullptr;
    if ( !buf || !len){
        return false;
    }
	seg = add_segment(buf, len,false);
	if (!seg)
        return false;
    seg->owns_data = false;
    return true;
}

bool seg_buf_t::alloc_and_push_back(size_t len)
{
    buf_t* seg = nullptr;
    uint8_t* buf = nullptr;
    if ( !len)
        return false;
    buf = new uint8_t[len];
	seg = add_segment(buf, len,false);
	if (!seg) {
	    delete[] buf;
        return false;
    }
    seg->owns_data = true;
    return true;
}

void seg_buf_t::incr_cur_seg_offset(uint64_t offset)
{
    buf_t* cur_seg = nullptr;
    cur_seg = segments[cur_seg_id];
    cur_seg->incr_offset(offset);
    if ((size_t)cur_seg->offset == cur_seg->len) {
        increment();
    }

}


/**
* Zero copy read of contiguous chunk from current segment.
* Returns false if unable to get a contiguous chunk, true otherwise
*/
bool seg_buf_t::zero_copy_read( uint8_t** ptr,
                                size_t chunk_len)
{
    buf_t* cur_seg = nullptr;
    cur_seg = segments[cur_seg_id];
    if (!cur_seg)
        return false;

    if ((size_t)cur_seg->offset + chunk_len <= cur_seg->len) {
        *ptr = cur_seg->buf + cur_seg->offset;
        read(nullptr, chunk_len);
        return true;
    }
    return false;
}

bool seg_buf_t::copy_to_contiguous_buffer(uint8_t* buffer)
{
	size_t i = 0;
    size_t offset = 0;

    if (!buffer)
        return false;

    for (i = 0; i < segments.size(); ++i) {
        buf_t* seg = segments[i];
        if (seg->len)
            memcpy(buffer + offset, seg->buf, seg->len);
        offset += seg->len;
    }
    return true;

}

uint8_t* seg_buf_t::get_global_ptr(void)
{
    buf_t* cur_seg = nullptr;
    cur_seg = segments[cur_seg_id];
    return (cur_seg) ? (cur_seg->buf + cur_seg->offset) : nullptr;
}

size_t seg_buf_t::get_cur_seg_len(void)
{
    buf_t* cur_seg = nullptr;
    cur_seg = segments[cur_seg_id];
    return (cur_seg) ? (cur_seg->len - (size_t)cur_seg->offset) : 0;
}

int64_t seg_buf_t::get_cur_seg_offset(void)
{
    buf_t* cur_seg = nullptr;
    cur_seg = segments[cur_seg_id];
    return (cur_seg) ? (int64_t)(cur_seg->offset) : 0;
}


int64_t seg_buf_t::get_global_offset(void)
{
    int64_t offset = 0;
    for (size_t i = 0; i < cur_seg_id; ++i) {
        buf_t* seg = segments[i];
        offset += (int64_t)seg->len;
    }
    return offset + get_cur_seg_offset();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

buf_t::~buf_t()
{
	if (buf && owns_data)
		delete[] buf;
}

void buf_t::incr_offset(uint64_t off)
{
    /*  we allow the offset to move to one location beyond end of buffer segment*/
    if (offset + off > (uint64_t)len) {
#ifdef DEBUG_SEG_BUF
        printf("[WARNING]  attempt to increment buffer offset out of bounds\n");
#endif
        offset = (uint64_t)len;
    }
    offset += off;
}


}
