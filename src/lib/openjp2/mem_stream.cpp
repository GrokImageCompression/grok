/**
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



static void grok_free_buffer_info(void* user_data)
{
	auto data = (buf_info_t*)user_data;
	if (data)
		delete data;
}

static size_t zero_copy_read_from_buffer(void ** p_buffer,
        size_t p_nb_bytes,
        buf_info_t* p_source_buffer)
{
    size_t l_nb_read = 0;

    if (((size_t)p_source_buffer->off + p_nb_bytes) < p_source_buffer->len) {
        l_nb_read = p_nb_bytes;
    }

    *p_buffer = p_source_buffer->buf + p_source_buffer->off;
    p_source_buffer->off += (int64_t)l_nb_read;

    return l_nb_read ? l_nb_read : ((size_t)-1);
}

static size_t grok_read_from_buffer(void * p_buffer,
                                   size_t p_nb_bytes,
                                   buf_info_t* p_source_buffer)
{
    size_t l_nb_read;

    if ((size_t)p_source_buffer->off + p_nb_bytes < p_source_buffer->len) {
        l_nb_read = p_nb_bytes;
    } else {
        l_nb_read = (p_source_buffer->len - (size_t)p_source_buffer->off);
    }
    memcpy(p_buffer, p_source_buffer->buf + p_source_buffer->off, l_nb_read);
    p_source_buffer->off += (int64_t)l_nb_read;

    return l_nb_read ? l_nb_read : ((size_t)-1);
}

static size_t grok_write_to_buffer(void * p_buffer,
                                    size_t p_nb_bytes,
                                    buf_info_t* p_source_buffer)
{
	if (p_source_buffer->off + p_nb_bytes >=  p_source_buffer->len) {
		return 0;
	}
	if (p_nb_bytes) {
		memcpy(p_source_buffer->buf + (size_t)p_source_buffer->off, p_buffer, p_nb_bytes);
		p_source_buffer->off += (int64_t)p_nb_bytes;
	}
    return p_nb_bytes;
}

static bool seek_from_buffer(int64_t p_nb_bytes,
                                 buf_info_t * p_source_buffer)
{
	if (p_nb_bytes < 0)
		return false;
    if ((size_t)p_nb_bytes <  p_source_buffer->len) {
        p_source_buffer->off = p_nb_bytes;
    } else {
        p_source_buffer->off = p_source_buffer->len;
    }
    return true;
}


static void set_up_buffer_stream(opj_stream_t* l_stream, size_t len, bool p_is_read_stream)
{
    opj_stream_set_user_data_length(l_stream, len);

    if (p_is_read_stream) {
        opj_stream_set_read_function(l_stream, (opj_stream_read_fn)grok_read_from_buffer);
        opj_stream_set_zero_copy_read_function(l_stream, (opj_stream_zero_copy_read_fn)zero_copy_read_from_buffer);
    } else
        opj_stream_set_write_function(l_stream, (opj_stream_write_fn)grok_write_to_buffer);
    opj_stream_set_seek_function(l_stream, (opj_stream_seek_fn)seek_from_buffer);
}

size_t get_buffer_stream_offset(opj_stream_t* stream) {
	if (!stream)
		return 0;
	GrokStream * private_stream = (GrokStream*)stream;
	if (!private_stream->m_user_data)
		return 0;
	buf_info_t* buf = (buf_info_t*)private_stream->m_user_data;
	return buf->off;
}

opj_stream_t*  create_buffer_stream(uint8_t *buf,
                                        size_t len, 
										bool ownsBuffer,
                                        bool p_is_read_stream)
{
    if (!buf || !len) {
        return nullptr;
    }
	GrokStream* l_stream = new GrokStream(buf, len, p_is_read_stream);
	auto p_source_buffer = new buf_info_t(buf, 0,len, ownsBuffer);
    opj_stream_set_user_data((opj_stream_t*)l_stream, p_source_buffer, grok_free_buffer_info);
    set_up_buffer_stream((opj_stream_t*)l_stream, p_source_buffer->len, p_is_read_stream);

    return (opj_stream_t*)l_stream;
}





int32_t get_file_open_mode(const char* mode)
{
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

static uint64_t  size_proc(grok_handle_t fd)
{
    ULARGE_INTEGER m;
    m.LowPart = GetFileSize(fd, &m.HighPart);
    return(m.QuadPart);
}


static void* grok_map(grok_handle_t fd, size_t len)
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

static grok_handle_t open_fd(const char* fname, const char* mode)
{
    void*	fd = nullptr;
    int32_t m = -1;
    DWORD			dwMode = 0;

    if (!fname)
        return (grok_handle_t)-1;


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

    fd = (grok_handle_t)CreateFileA(fname,
                                   (m == O_RDONLY) ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE),
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, dwMode,
                                   (m == O_RDONLY) ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
    if (fd == INVALID_HANDLE_VALUE) {
        return (grok_handle_t)-1;
    }
    return fd;
}

static int32_t close_fd(grok_handle_t fd)
{
    int32_t rc = -1;
    if (fd) {
        rc = CloseHandle(fd) ? 0 : -1;
    }
    return rc;
}

#else

static uint64_t size_proc(grok_handle_t fd)
{
    struct stat sb;
    if (!fd)
        return 0;

    if (fstat(fd, &sb)<0)
        return(0);
    else
        return((uint64_t)sb.st_size);
}

static void* grok_map(grok_handle_t fd, size_t len)
{
	(void)len;
    void* ptr = nullptr;
    uint64_t		size64 = 0;

    if (!fd)
        return nullptr;

    size64 = size_proc(fd);
    ptr = (void*)mmap(0, (size_t)size64, PROT_READ, MAP_SHARED, fd, 0);
    return ptr == (void*)-1 ? nullptr : ptr;
}

static int32_t unmap(void* ptr, size_t len)
{
    if (ptr)
        munmap(ptr, len);
    return 0;
}

static grok_handle_t open_fd(const char* fname, const char* mode)
{
    grok_handle_t	fd = 0;
    int32_t m = -1;
    if (!fname) {
        return (grok_handle_t)-1;
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
        return (grok_handle_t)-1;
    }
    return fd;
}

static int32_t close_fd(grok_handle_t fd)
{
    if (!fd)
        return 0;
    return  (close(fd));
}

#endif



static void mem_map_free(void* user_data)
{
    if (user_data) {
        buf_info_t* buffer_info = (buf_info_t*)user_data;
        unmap(buffer_info->buf, buffer_info->len);
        close_fd(buffer_info->fd);
        grok_free(buffer_info);
    }
}

/*
Currently, only read streams are supported for memory mapped files.
*/
opj_stream_t* create_mapped_file_read_stream(const char *fname)
{
    opj_stream_t*	l_stream = nullptr;
    buf_info_t* buffer_info = nullptr;
    void*			mapped_view = nullptr;
    bool p_is_read_stream = true;

    grok_handle_t	fd = open_fd(fname, p_is_read_stream ? "r" : "w");
    if (fd == (grok_handle_t)-1)
        return nullptr;

    buffer_info = (buf_info_t*)grok_malloc(sizeof(buf_info_t));
    memset(buffer_info, 0, sizeof(buf_info_t));
    buffer_info->fd = fd;
    buffer_info->len = (size_t)size_proc(fd);

    l_stream = opj_stream_create(0, p_is_read_stream);
    if (!l_stream) {
        mem_map_free(buffer_info);
        return nullptr;
    }

    mapped_view = grok_map(fd, buffer_info->len);
    if (!mapped_view) {
        opj_stream_destroy(l_stream);
        mem_map_free(buffer_info);
        return nullptr;
    }

    buffer_info->buf = (uint8_t*)mapped_view;
    buffer_info->off = 0;

    opj_stream_set_user_data(l_stream, buffer_info, (opj_stream_free_user_data_fn)mem_map_free);
    set_up_buffer_stream(l_stream, buffer_info->len, p_is_read_stream);


    return l_stream;
}


}
