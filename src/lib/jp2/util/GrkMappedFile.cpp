#include "grk_includes.h"

#ifdef _WIN32
#include <windows.h>
#else /* _WIN32 */
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#endif
#include <fcntl.h>

namespace grk {

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

static uint64_t  size_proc(grk_handle fd){
	LARGE_INTEGER filesize = { 0 };
	if (GetFileSizeEx(fd, &filesize))
		return (uint64_t)filesize.QuadPart;
	return 0;
}


static void* grk_map(grk_handle fd, size_t len, bool do_read){
    void* ptr = nullptr;
    HANDLE hMapFile = nullptr;

    if (!fd || !len)
        return nullptr;

    /* Passing in 0 for the maximum file size indicates that we
    would like to create a file mapping object for the full file size */
    hMapFile = CreateFileMapping(fd, nullptr, do_read ? PAGE_READONLY : PAGE_READWRITE, 0, 0, nullptr);
    if (hMapFile == nullptr) {
        return nullptr;
    }
    ptr = MapViewOfFile(hMapFile, do_read ? FILE_MAP_READ : FILE_MAP_WRITE, 0, 0, 0);
    CloseHandle(hMapFile);
    return ptr;
}

static int32_t unmap(void* ptr, size_t len){
    int32_t rc = -1;
    (void)len;
    if (ptr)
        rc = UnmapViewOfFile(ptr) ? 0 : -1;
    return rc;
}

static grk_handle open_fd(const char* fname, const char* mode){
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
    if (fd == INVALID_HANDLE_VALUE)
        return (grk_handle)-1;

    return fd;
}

static int32_t close_fd(grk_handle fd){
    int32_t rc = -1;
    if (fd)
        rc = CloseHandle(fd) ? 0 : -1;

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

static void* grk_map(grk_handle fd, size_t len, bool do_read) {
	(void) len;
	if (!fd)
		return nullptr;
	void* ptr = nullptr;

	if (do_read)
		ptr = (void*) mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, 0);
	else
		ptr = (void*) mmap(nullptr, len, PROT_WRITE, MAP_SHARED, fd, 0);
	return ptr == (void*) -1 ? nullptr : ptr;
}

static int32_t unmap(void *ptr, size_t len) {
	int32_t rc = -1;
	if (ptr)
		rc = munmap(ptr, len);
	return rc;
}

static grk_handle open_fd(const char *fname, const char *mode) {
	grk_handle fd = 0;
	int32_t m = -1;
	if (!fname)
		return (grk_handle) -1;
	m = get_file_open_mode(mode);
	if (m == -1)
		return (grk_handle) -1;
	fd = open(fname, m, 0666);
	if (fd < 0) {
        if (errno > 0 && strerror(errno) != nullptr){
            GRK_ERROR("%s: %s", fname, strerror(errno));
        }
        else {
        	GRK_ERROR("%s: Cannot open", fname);
        }
		return (grk_handle) -1;
	}
	return fd;
}

static int32_t close_fd(grk_handle fd) {
	if (!fd)
		return 0;
	return close(fd);
}

#endif

static void mem_map_free(void *user_data) {
	if (user_data) {
		buf_info *buffer_info = (buf_info*) user_data;
		int32_t rc = unmap(buffer_info->buf, buffer_info->len);
		if (rc)
			GRK_ERROR("Unmapping memory mapped file failed with error %u", rc);
		rc = close_fd(buffer_info->fd);
		if (rc)
			GRK_ERROR("Closing memory mapped file failed with error %u", rc);
		delete buffer_info;
	}
}

grk_stream* create_mapped_file_read_stream(const char *fname) {
	grk_handle fd = open_fd(fname, "r");
	if (fd == (grk_handle) -1){
		GRK_ERROR("Unable to open memory mapped file %s", fname);
		return nullptr;
	}

	auto buffer_info = new buf_info();
	buffer_info->fd = fd;
	buffer_info->len = (size_t) size_proc(fd);
	auto mapped_view = grk_map(fd, buffer_info->len, true);
	if (!mapped_view) {
		GRK_ERROR("Unable to map memory mapped file %s", fname);
		mem_map_free(buffer_info);
		return nullptr;
	}
	buffer_info->buf = (uint8_t*) mapped_view;
	buffer_info->off = 0;

	// now treat mapped file like any other memory stream
	auto l_stream = (grk_stream*) (new BufferedStream(buffer_info->buf,
			buffer_info->len, true));
	grk_stream_set_user_data(l_stream, buffer_info,
			(grk_stream_free_user_data_fn) mem_map_free);
	set_up_mem_stream(l_stream, buffer_info->len, true);

	return l_stream;
}

grk_stream* create_mapped_file_write_stream(const char *fname) {
	GRK_ERROR("Memory mapped file writing not currently supported");
	return nullptr;

	grk_handle fd = open_fd(fname, "w");
	if (fd == (grk_handle) -1){
		GRK_ERROR("Unable to open memory mapped file %s", fname);
		return nullptr;
	}

	auto buffer_info = new buf_info();
	buffer_info->fd = fd;
	auto mapped_view = grk_map(fd, buffer_info->len, false);
	if (!mapped_view) {
		GRK_ERROR("Unable to map memory mapped file %s", fname);
		mem_map_free(buffer_info);
		return nullptr;
	}
	buffer_info->buf = (uint8_t*) mapped_view;
	buffer_info->off = 0;

	// now treat mapped file like any other memory stream
	auto l_stream = (grk_stream*) (new BufferedStream(buffer_info->buf,
			buffer_info->len, true));
	grk_stream_set_user_data(l_stream, buffer_info,
			(grk_stream_free_user_data_fn) mem_map_free);
	set_up_mem_stream(l_stream, buffer_info->len, false);

	return l_stream;
}


}
