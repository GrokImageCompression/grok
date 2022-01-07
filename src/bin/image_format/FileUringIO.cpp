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
 *
 */
#include "grk_apps_config.h"

#ifndef GROK_HAVE_URING
#error GROK_HAVE_URING_NOT_DEFINED
#endif

#include "FileUringIO.h"
#include "common.h"
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

#define QD 1024
#define BS (32 * 1024)
struct io_uring ring;

struct io_data
{
	io_data() : readop(false), offset(0), iov{0, 0} {}
	bool readop;
	uint64_t offset;
	iovec iov;
};

static void enqueue(io_uring* ring, io_data* data, int fd)
{
	auto sqe = io_uring_get_sqe(ring);
	assert(sqe);

	if(data->readop)
		io_uring_prep_readv(sqe, fd, &data->iov, 1, data->offset);
	else
		io_uring_prep_writev(sqe, fd, &data->iov, 1, data->offset);
	io_uring_sqe_set_data(sqe, data);
	io_uring_submit(ring);
}

FileUringIO::FileUringIO() : m_fd(0), m_off(0), m_queueCount(0)
{
	memset(&ring, 0, sizeof(ring));
}

FileUringIO::~FileUringIO()
{
	close();
}

void FileUringIO::attach(std::string fileName, std::string mode, int fd){
	m_fileName = fileName;
	bool useStdio = grk::useStdio(m_fileName.c_str());
	bool doRead = mode[0] == -'r';
	if(useStdio)
		m_fd = doRead ? STDIN_FILENO : STDOUT_FILENO;
	else
		m_fd = fd;
}

bool FileUringIO::open(std::string fileName, std::string mode)
{
	m_fileName = fileName;
	bool useStdio = grk::useStdio(m_fileName.c_str());
	bool doRead = mode[0] == -'r';
	if(useStdio)
	{
		m_fd = doRead ? STDIN_FILENO : STDOUT_FILENO;
		return true;
	}
	auto name = m_fileName.c_str();
	m_fd = ::open(name, getMode(mode.c_str()), 0666);
	if(m_fd < 0)
	{
		if(errno > 0 && strerror(errno) != nullptr)
			spdlog::error("{}: {}", name, strerror(errno));
		else
			spdlog::error("{}: Cannot open", name);
		return false;
	}
	// initialize ring
	if(!doRead)
	{
		int ret = io_uring_queue_init(QD, &ring, 0);
		if(ret < 0)
		{
			spdlog::error("queue_init: %s\n", strerror(-ret));
			return false;
		}
	}

	return true;
}

int FileUringIO::getMode(const char* mode)
{
	int m = -1;

	switch(mode[0])
	{
		case 'r':
			m = O_RDONLY;
			if(mode[1] == '+')
				m = O_RDWR;
			break;
		case 'w':
			m = O_WRONLY | O_CREAT | O_TRUNC;
			break;
		case 'a':
			m = O_RDWR | O_CREAT;
			break;
		default:
			spdlog::error("Bad mode {}", mode);
			break;
	}
	return m;
}

bool FileUringIO::close(void)
{
	bool rc = false;
	if(m_fd)
	{
		if(fsync(m_fd))
			spdlog::error("failed to synch file");
	}
	if(ring.ring_fd)
	{
		// process completions
		for(uint32_t i = 0; i < m_queueCount; ++i)
		{
			io_uring_cqe* cqe;
			int ret = io_uring_wait_cqe(&ring, &cqe);

			if(ret < 0)
			{
				spdlog::error("io_uring_wait_cqe");
				rc = false;
			}
			if(cqe->res < 0)
			{
				spdlog::error("The system call invoked asynchronously failed");
				rc = false;
			}

			/* Retrieve user data from CQE */
			auto data = (io_data*)io_uring_cqe_get_data(cqe);

			/* process this request here */
			if(!data->readop)
				delete[](uint8_t*) data->iov.iov_base;
			delete data;

			/* Mark this completion as seen */
			io_uring_cqe_seen(&ring, cqe);
		}
		io_uring_queue_exit(&ring);
		memset(&ring, 0, sizeof(ring));
	}
	if(!m_fd || grk::useStdio(m_fileName.c_str()))
		rc = true;
	else if(::close(m_fd) == 0)
		rc = true;
	m_fd = 0;

	return rc;
}

bool FileUringIO::write(uint8_t* buf, size_t len)
{
	bool rc = true;
	// auto start = std::chrono::high_resolution_clock::now();
	io_data* data = new io_data();
	auto b = new uint8_t[len];
	memcpy(b, buf, len);
	data->offset = m_off;
	m_off += (uint64_t)len;
	data->iov.iov_base = b;
	data->iov.iov_len = len;
	enqueue(&ring, data, m_fd);

	// auto finish = std::chrono::high_resolution_clock::now();
	// std::chrono::duration<double> elapsed = finish - start;
	// spdlog::info("write time: {} ms",	elapsed.count() * 1000);
	m_queueCount++;

	return rc;
}
bool FileUringIO::read(uint8_t* buf, size_t len)
{
	/*
		bool rc = true;
		// auto start = std::chrono::high_resolution_clock::now();
		io_data* data = new io_data();
		data->readop = false;
		data->offset = m_off;
		m_off += (uint64_t)len;
		data->iov.iov_base = buf;
		data->iov.iov_len = len;
		queue_write(&ring, data, m_fd);

		// auto finish = std::chrono::high_resolution_clock::now();
		// std::chrono::duration<double> elapsed = finish - start;
		// spdlog::info("write time: {} ms",	elapsed.count() * 1000);
		m_queueCount++;

		return rc;
	*/

	auto actual = (size_t)::read(m_fd, buf, len);
	if(actual < len)
		spdlog::error("read fewer bytes {} than expected number of bytes {}.", actual, len);

	return actual == len;
}
bool FileUringIO::seek(int64_t pos)
{
	return lseek(m_fd, pos, SEEK_SET) == pos;
}
