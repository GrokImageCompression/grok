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
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include "grok.h"

/* opaque stream object */
typedef grk_object grk_stream;

/*
 * read callback
 *
 */
typedef size_t (*grk_stream_read_fn)(uint8_t* buffer, size_t numBytes, void* user_data);

/*
 * write callback
 */
typedef size_t (*grk_stream_write_fn)(const uint8_t* buffer, size_t numBytes, void* user_data);
/*
 * (absolute) seek callback
 */
typedef bool (*grk_stream_seek_fn)(uint64_t numBytes, void* user_data);
/*
 *  free user data callback
 */
typedef void (*grk_stream_free_user_data_fn)(void* user_data);

/**
 * Set read function
 *
 * @param       stream      JPEG 2000 stream
 * @param       func        read function
 */
void grk_stream_set_read_function(grk_stream* stream, grk_stream_read_fn func);

/**
 * Set write function
 *
 * @param       stream      JPEG 2000 stream
 * @param       func        write function
 */
void grk_stream_set_write_function(grk_stream* stream, grk_stream_write_fn func);

/**
 * Set (absolute) seek function (stream must be seekable)
 *
 * @param       stream      JPEG 2000 stream
 * @param       func        (absolute) seek function.
 */
void grk_stream_set_seek_function(grk_stream* stream, grk_stream_seek_fn func);

/**
 * Set user data for JPEG 2000 stream
 *
 * @param       stream      JPEG 2000 stream
 * @param       data        user data
 * @param       func        function to free data when grk_object_unref() is called.
 */
void grk_stream_set_user_data(grk_stream* stream, void* data, grk_stream_free_user_data_fn func);

/**
 * Set the length of the user data for the stream.
 *
 * @param stream    JPEG 2000 stream
 * @param data_length length of data.
 */
void grk_stream_set_user_data_length(grk_stream* stream, uint64_t data_length);

/** Create stream from a file identified with its filename with a specific buffer size
 *
 * @param fname           the name of the file to stream
 * @param buffer_size     size of the chunk used to stream
 * @param is_read_stream  whether the stream is a read stream (true) or not (false)
 */
grk_stream* grk_stream_create_file_stream(const char* fname, size_t buffer_size,
										  bool is_read_stream);

/** Create stream from buffer
 *
 * @param buf           buffer
 * @param buffer_len    length of buffer
 * @param ownsBuffer    if true, library will delete[] buffer. Otherwise, it is the caller's
 *                      responsibility to delete the buffer
 * @param is_read_stream  whether the stream is a read stream (true) or not (false)
 */
grk_stream* grk_stream_create_mem_stream(uint8_t* buf, size_t buffer_len, bool ownsBuffer,
										 bool is_read_stream);

/**
 * Get length of memory stream
 *
 * @param stream memory stream
 */
size_t grk_stream_get_write_mem_stream_length(grk_stream* stream);

/**
 * Creates a J2K/JP2 compression codec
 *
 * @param   format      output format : j2k or jp2
 * @param   stream      JPEG 2000 stream
 *
 * @return              compression codec if successful, otherwise NULL
 */
grk_codec* grk_compress_create(GRK_CODEC_FORMAT format, grk_stream* stream);

/**
 * Start compressing image
 *
 * @param codec         compression codec
 *
 */
bool grk_compress_start(grk_codec* codec);
