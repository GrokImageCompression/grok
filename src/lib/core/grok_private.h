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


