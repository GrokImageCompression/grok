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
 */


#include <FileUringIO.h>
#include "common.h"
#include "grk_apps_config.h"

#ifndef GROK_HAVE_URING
# error GROK_HAVE_URING_NOT_DEFINED
#endif

#include <strings.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <chrono>

#include <liburing.h>
#include <liburing/io_uring.h>

#define QD	1024
#define BS	(32*1024)
struct io_uring ring;

struct io_data {
	io_data() : offset(0),
				iov{0,0}
	{}
	off_t offset;
	struct iovec iov;
};

static int setup_context(unsigned entries, io_uring *ring)
{
	int ret = io_uring_queue_init(entries, ring, 0);
	if (ret < 0) {
		fprintf(stderr, "queue_init: %s\n", strerror(-ret));
		return -1;
	}

	return 0;
}

static void queue_prepped(io_uring *ring, io_data *data, int outfd)
{
	io_uring_sqe *sqe = io_uring_get_sqe(ring);
	assert(sqe);

	io_uring_prep_writev(sqe, outfd, &data->iov, 1, data->offset);
	io_uring_sqe_set_data(sqe, data);
}

static void queue_write(io_uring *ring, io_data *data,int outfd)
{
	queue_prepped(ring, data,outfd);
	io_uring_submit(ring);
}

int
_getMode(const char* mode)
{
	int m = -1;

	switch (mode[0]) {
	case 'r':
		m = O_RDONLY;
		if (mode[1] == '+')
			m = O_RDWR;
		break;
	case 'w':
		m = O_WRONLY | O_CREAT | O_TRUNC;
		break;
	case 'a':
		m = O_RDWR|O_CREAT;
		break;
	default:
		spdlog::error("Bad mode {}", mode);
		break;
	}
	return (m);
}



FileUringIO::FileUringIO()  : m_fd(0), m_off(0)
{
	memset(&ring, 0, sizeof(ring));
}

FileUringIO::~FileUringIO()
{
	close();
	if (ring.ring_fd)
	  io_uring_queue_exit(&ring);
}

bool FileUringIO::open(std::string fileName, std::string mode){
	bool useStdio = grk::useStdio(fileName.c_str());
	bool doRead = mode[0] ==- 'r';
	if (useStdio){
		m_fd = doRead ? STDIN_FILENO : STDOUT_FILENO;
		return true;
	}
	int m = _getMode(mode.c_str());
	const char* name = fileName.c_str();

	m_fd = ::open(name, m, 0666);
	if (m_fd < 0) {
		if (errno > 0 && strerror(errno) != NULL ) {
			spdlog::error("{}: {}", name, strerror(errno) );
		} else {
			spdlog::error("{}: Cannot open", name);
		}
		return false;
	}
	m_fileName = fileName;
	if (!doRead) {
		if (setup_context(QD, &ring))
			return false;
	}
	return true;

}
bool FileUringIO::close(void){
	bool rc = false;
	if (!m_fd || grk::useStdio(m_fileName.c_str()))
		rc =  true;
	else if (::close(m_fd) == 0)
		rc = true;
	m_fd = 0;
	return rc;
}

bool do_uring = true;

bool FileUringIO::write(uint8_t *buf, size_t len){

	bool rc = true;
	auto start = std::chrono::high_resolution_clock::now();

	if (do_uring) {
		io_data *data = new io_data();
		data->offset = m_off;
		m_off += len;
		data->iov.iov_base = buf;
		data->iov.iov_len = len;
		queue_write(&ring, data, m_fd);
	} else {
		auto actual = (size_t)::write(m_fd, buf, len);
		if (actual < len)
			spdlog::error("wrote fewer bytes {} than expected number of bytes {}.",actual, len);
		rc = actual == len;
	}

	auto finish = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> elapsed = finish - start;
	//spdlog::info("write time: {} ms",	elapsed.count() * 1000);


	return rc;
}
bool FileUringIO::read(uint8_t *buf, size_t len){
	auto actual =  (size_t)::read(m_fd, buf, len);
	if (actual < len)
		spdlog::error("read fewer bytes {} than expected number of bytes {}.",actual, len);

	return actual == len;
}
bool FileUringIO::seek(size_t pos){
	return   (size_t)lseek(m_fd, pos, SEEK_SET) == pos;
}
