/**
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

#ifdef _WIN32
#include <windows.h>
#else /* _WIN32 */

#include <errno.h>

#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
# include <unistd.h>
# include <sys/mman.h>

#endif

#include <fcntl.h>

namespace grk {

static void free_mem(void *user_data) {
	auto data = (buf_info*) user_data;
	if (data)
		delete data;
}

static size_t zero_copy_read_from_mem(void **p_buffer, size_t nb_bytes,
		buf_info *p_source_buffer) {
	size_t nb_read = 0;

	if (((size_t) p_source_buffer->off + nb_bytes) < p_source_buffer->len) {
		nb_read = nb_bytes;
	}

	*p_buffer = p_source_buffer->buf + p_source_buffer->off;
	assert(p_source_buffer->off + nb_read <= p_source_buffer->len);
	p_source_buffer->off += nb_read;

	return nb_read;
}

static size_t read_from_mem(void *p_buffer, size_t nb_bytes,
		buf_info *p_source_buffer) {
	size_t nb_read;

	if (!p_buffer)
		return 0;

	if ( p_source_buffer->off + nb_bytes < p_source_buffer->len) {
		nb_read = nb_bytes;
	} else {
		nb_read = (size_t)(p_source_buffer->len - p_source_buffer->off);
	}

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

static size_t write_to_mem(void *dest, size_t nb_bytes,
		buf_info *src) {
	if (src->off + nb_bytes >= src->len) {
		return 0;
	}
	if (nb_bytes) {
		memcpy(src->buf + (size_t) src->off, dest,
				nb_bytes);
		src->off += nb_bytes;
	}
	return nb_bytes;
}

static bool seek_from_mem(uint64_t nb_bytes, buf_info *src) {
	if (nb_bytes < src->len) {
		src->off = nb_bytes;
	} else {
		src->off = src->len;
	}
	return true;
}

static void set_up_mem_stream( grk_stream  *l_stream, size_t len,
		bool p_is_read_stream) {
	grk_stream_set_user_data_length(l_stream, len);

	if (p_is_read_stream) {
		grk_stream_set_read_function(l_stream,
				(grk_stream_read_fn) read_from_mem);
		grk_stream_set_zero_copy_read_function(l_stream,
				(grk_stream_zero_copy_read_fn) zero_copy_read_from_mem);
	} else
		grk_stream_set_write_function(l_stream,
				(grk_stream_write_fn) write_to_mem);
	grk_stream_set_seek_function(l_stream,
			(grk_stream_seek_fn) seek_from_mem);
}

size_t get_mem_stream_offset( grk_stream  *stream) {
	if (!stream)
		return 0;
	auto private_stream = (BufferedStream*) stream;
	if (!private_stream->m_user_data)
		return 0;
	auto buf = (buf_info*) private_stream->m_user_data;
	return buf->off;
}

 grk_stream  *  create_mem_stream(uint8_t *buf, size_t len, bool ownsBuffer,
		bool p_is_read_stream) {
	if (!buf || !len) {
		return nullptr;
	}
	auto l_stream = new BufferedStream(buf, len, p_is_read_stream);
	auto p_source_buffer = new buf_info(buf, 0, len, ownsBuffer);
	grk_stream_set_user_data(( grk_stream  * ) l_stream, p_source_buffer,
			free_mem);
	set_up_mem_stream(( grk_stream  * ) l_stream, p_source_buffer->len,
			p_is_read_stream);

	return ( grk_stream  * ) l_stream;
}

static int32_t get_file_open_mode(const char *mode) {
	int32_t m = -1;
	switch (mode[0]) {
	case 'r':
		m = O_RDONLY;
		if (mode[1] == '+')
			m = O_RDWR;
		break;
	case 'w':
	case 'a':
		m = O_RDWR | O_CREAT;
		if (mode[0] == 'w')
			m |= O_TRUNC;
		break;
	default:
		break;
	}
	return m;
}

#ifdef _WIN32

static uint64_t  size_proc(grk_handle fd)
{
	LARGE_INTEGER filesize = { 0 };
	if (GetFileSizeEx(fd, &filesize))
		return (uint64_t)filesize.QuadPart;
	return 0;
}


static void* grk_map(grk_handle fd, size_t len)
{
	(void)len;
    void* ptr = nullptr;
    HANDLE hMapFile = nullptr;

    if (!fd || !len)
        return nullptr;

    /* Passing in 0 for the maximum file size indicates that we
    would like to create a file mapping object for the full file size */
    hMapFile = CreateFileMapping(fd, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (hMapFile == nullptr) {
        return nullptr;
    }
    ptr = MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hMapFile);
    return ptr;
}

static int32_t unmap(void* ptr, size_t len)
{
    int32_t rc = -1;
    (void)len;
    if (ptr)
        rc = UnmapViewOfFile(ptr) ? 0 : -1;
    return rc;
}

static grk_handle open_fd(const char* fname, const char* mode)
{
    void*	fd = nullptr;
    int32_t m = -1;
    DWORD			dwMode = 0;

    if (!fname)
        return (grk_handle)-1;


    m = get_file_open_mode(mode);
    switch (m) {
    case O_RDONLY:
        dwMode = OPEN_EXISTING;
        break;
    case O_RDWR:
        dwMode = OPEN_ALWAYS;
        break;
    case O_RDWR | O_CREAT:
        dwMode = OPEN_ALWAYS;
        break;
    case O_RDWR | O_TRUNC:
        dwMode = CREATE_ALWAYS;
        break;
    case O_RDWR | O_CREAT | O_TRUNC:
        dwMode = CREATE_ALWAYS;
        break;
    default:
        return nullptr;
    }

    fd = (grk_handle)CreateFileA(fname,
                                   (m == O_RDONLY) ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE),
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, dwMode,
                                   (m == O_RDONLY) ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
    if (fd == INVALID_HANDLE_VALUE) {
        return (grk_handle)-1;
    }
    return fd;
}

static int32_t close_fd(grk_handle fd)
{
    int32_t rc = -1;
    if (fd) {
        rc = CloseHandle(fd) ? 0 : -1;
    }
    return rc;
}

#else

static uint64_t size_proc(grk_handle fd) {
	struct stat sb;
	if (!fd)
		return 0;

	if (fstat(fd, &sb) < 0)
		return (0);
	else
		return ((uint64_t) sb.st_size);
}

static void* grk_map(grk_handle fd, size_t len) {
	(void) len;
	void *ptr = nullptr;
	uint64_t size64 = 0;

	if (!fd)
		return nullptr;

	size64 = size_proc(fd);
	ptr = (void*) mmap(0, (size_t) size64, PROT_READ, MAP_SHARED, fd, 0);
	return ptr == (void*) -1 ? nullptr : ptr;
}

static int32_t unmap(void *ptr, size_t len) {
	if (ptr)
		munmap(ptr, len);
	return 0;
}

static grk_handle open_fd(const char *fname, const char *mode) {
	grk_handle fd = 0;
	int32_t m = -1;
	if (!fname) {
		return (grk_handle) -1;
	}
	m = get_file_open_mode(mode);
	fd = open(fname, m, 0666);
	if (fd < 0) {
#ifdef DEBUG_ERRNO
        if (errno > 0 && strerror(errno) != nullptr) {
            printf("%s: %s", fname, strerror(errno));
        } else {
            printf("%s: Cannot open", fname);
        }
#endif
		return (grk_handle) -1;
	}
	return fd;
}

static int32_t close_fd(grk_handle fd) {
	if (!fd)
		return 0;
	return (close(fd));
}

#endif

static void mem_map_free(void *user_data) {
	if (user_data) {
		buf_info *buffer_info = (buf_info*) user_data;
		unmap(buffer_info->buf, buffer_info->len);
		close_fd(buffer_info->fd);
		delete buffer_info;
	}
}

/*
 Currently, only read streams are supported for memory mapped files.
 */
 grk_stream  *  create_mapped_file_read_stream(const char *fname) {
	bool p_is_read_stream = true;

	grk_handle fd = open_fd(fname, p_is_read_stream ? "r" : "w");
	if (fd == (grk_handle) -1)
		return nullptr;

	auto buffer_info = new buf_info();
	buffer_info->fd = fd;
	buffer_info->len = (size_t) size_proc(fd);
	auto mapped_view = grk_map(fd, buffer_info->len);
	if (!mapped_view) {
		mem_map_free(buffer_info);
		return nullptr;
	}
	buffer_info->buf = (uint8_t*) mapped_view;
	buffer_info->off = 0;

	// now treat mapped file like any other memory stream
	auto l_stream = (grk_stream*)(new BufferedStream(buffer_info->buf, buffer_info->len, p_is_read_stream));
	grk_stream_set_user_data(l_stream, buffer_info,
			(grk_stream_free_user_data_fn) mem_map_free);
	set_up_mem_stream(l_stream, buffer_info->len, p_is_read_stream);

	return l_stream;
}

}
