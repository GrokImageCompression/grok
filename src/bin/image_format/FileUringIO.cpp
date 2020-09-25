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

FileUringIO::FileUringIO()  : m_fd(0), m_off(0), m_writeCount(0)
{
	memset(&ring, 0, sizeof(ring));
}

FileUringIO::~FileUringIO()
{
	close();
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

int process_completion(struct io_uring *ring) {
    struct io_uring_cqe *cqe;
    int ret = io_uring_wait_cqe(ring, &cqe);

    if (ret < 0) {
        perror("io_uring_wait_cqe");
        return 1;
    }

    if (cqe->res < 0) {
        /* The system call invoked asynchonously failed */
        return 1;
    }

    /* Retrieve user data from CQE */
    io_data *data = (io_data*)io_uring_cqe_get_data(cqe);
    delete[] (uint8_t*)data->iov.iov_base;
    delete data;
    /* process this request here */

    /* Mark this completion as seen */
    io_uring_cqe_seen(ring, cqe);
    return 0;
}

bool FileUringIO::close(void){
	bool rc = false;
	if (m_fd){
		if (fsync(m_fd))
			spdlog::error("failed to synch file");
	}
	if (ring.ring_fd){
		for (uint32_t i = 0; i < m_writeCount; ++i)
			process_completion(&ring);
	    io_uring_queue_exit(&ring);
		memset(&ring, 0, sizeof(ring));
	}
	if (!m_fd || grk::useStdio(m_fileName.c_str()))
		rc =  true;
	else if (::close(m_fd) == 0)
		rc = true;
	m_fd = 0;
	return rc;
}

bool FileUringIO::write(uint8_t *buf, size_t len){

	bool rc = true;
	auto start = std::chrono::high_resolution_clock::now();
	io_data *data = new io_data();
	auto b = new uint8_t[len];
	memcpy(b,buf,len);
	data->offset = m_off;
	m_off += len;
	data->iov.iov_base = b;
	data->iov.iov_len = len;
	queue_write(&ring, data, m_fd);

	auto finish = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> elapsed = finish - start;
	//spdlog::info("write time: {} ms",	elapsed.count() * 1000);
	m_writeCount++;


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
