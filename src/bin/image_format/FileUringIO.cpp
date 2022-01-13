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

#ifdef GROK_HAVE_URING

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
#include "MemManager.h"


FileUringIO::FileUringIO() : m_fd(0),
							ownsDescriptor(false),
							m_queueCount(0)
{
	memset(&ring, 0, sizeof(ring));
}

FileUringIO::~FileUringIO()
{
	close();
}

bool FileUringIO::attach(std::string fileName, std::string mode, int fd){
	m_fileName = fileName;
	bool useStdio = grk::useStdio(m_fileName.c_str());
	bool doRead = mode[0] == -'r';
	if(useStdio)
		m_fd = doRead ? STDIN_FILENO : STDOUT_FILENO;
	else
		m_fd = fd;
	ownsDescriptor = false;

	return (doRead ? true : initQueue());
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
	ownsDescriptor = true;

	return (doRead ? true : initQueue());
}

bool FileUringIO::initQueue(void){
	int ret = io_uring_queue_init(QD, &ring, 0);
	if(ret < 0)
	{
		spdlog::error("queue_init: %s\n", strerror(-ret));
		close();
		return false;
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

void FileUringIO::enqueue(io_uring* ring,
						io_data* data,
						grk_serialize_buf* reclaimed,
						uint32_t max_reclaimed,
						uint32_t *num_reclaimed,
						bool readop,
						int fd)
{
	auto sqe = io_uring_get_sqe(ring);
	assert(sqe);

	//grk::ChronoTimer timer("uring: time to enque");
	//timer.start();
	assert(data->buf.data == data->iov.iov_base);
	if(readop)
		io_uring_prep_readv(sqe, fd, &data->iov, 1, data->buf.offset);
	else
		io_uring_prep_writev(sqe, fd, &data->iov, 1, data->buf.offset);
	io_uring_sqe_set_data(sqe, data);
	int ret = io_uring_submit(ring);
	printf("Enqueued  %p, length %d, offset %d\n", data->buf.data, data->buf.dataLen, data->buf.offset);
	//timer.finish();
	assert(ret == 1);
	(void)ret;
	m_queueCount++;

	// reclaim
	bool canReclaim = reclaimed && max_reclaimed > 0 && num_reclaimed;
	if (canReclaim)
		*num_reclaimed = 0;
	while (true) {
		bool success;
		auto data = retrieveCompletion(true,success);
		if (!success || !data)
			break;
		if (data->buf.pooled) {
			if (canReclaim && *num_reclaimed < max_reclaimed){
				reclaimed[*num_reclaimed] = data->buf;
				(*num_reclaimed)++;
			} else {
				grk::grkAlignedFree((uint8_t*)data->iov.iov_base);
			}
		}
		delete data;
	}
}

io_data* FileUringIO::retrieveCompletion(bool peek, bool &success){
	io_uring_cqe* cqe;
	int ret;

	if (peek)
		ret = io_uring_peek_cqe(&ring, &cqe);
	else
		ret = io_uring_wait_cqe(&ring, &cqe);

	if(ret < 0)
	{
		if (!peek) {
			spdlog::error("io_uring_wait_cqe returned an error.");
			success = false;
		}
		return nullptr;
	}
	if(cqe->res < 0)
	{
		spdlog::error("The system call invoked asynchronously has failed with the following error:"
							" \n%s", strerror(cqe->res));
		success = false;
		return nullptr;
	}

	success = true;
	auto data = (io_data*)io_uring_cqe_get_data(cqe);
	if (data) {
		io_uring_cqe_seen(&ring, cqe);
		assert(m_queueCount != 0);
		m_queueCount--;
	}

	return data;
}

bool FileUringIO::close(void)
{
	if(!m_fd)
		return true;
	if(ring.ring_fd)
	{
		//grk::ChronoTimer timer("uring: time to close");
		//timer.start();
		// process completions
		size_t count = m_queueCount;
		for(uint32_t i = 0; i < count; ++i)
		{
			bool success;
			auto data = retrieveCompletion(false,success);
			if (!success)
				break;
			if (data) {
				if (data->buf.pooled) {
					//printf("Close: deallocating  %p\n", data->iov.iov_base);
					grk::grkAlignedFree(data->iov.iov_base);
				}
				delete data;
			}
		}
		io_uring_queue_exit(&ring);
		memset(&ring, 0, sizeof(ring));
		//timer.finish();
	}
	assert(m_queueCount == 0);
	m_queueCount = 0;
	bool rc = false;
	if(grk::useStdio(m_fileName.c_str()) || !ownsDescriptor)
		rc = true;
	else if(::close(m_fd) == 0)
		rc = true;
	m_fd = 0;

	return rc;
}

bool FileUringIO::write(uint8_t* buf, uint64_t offset, size_t len, size_t maxLen,bool pooled)
{
	GrkSerializeBuf b = GrkSerializeBuf(buf,offset,len,maxLen,pooled);

	return write(b, nullptr,0,nullptr);
}
bool FileUringIO::write(GrkSerializeBuf buffer,
						grk_serialize_buf* reclaimed,
						uint32_t max_reclaimed,
						uint32_t *num_reclaimed) {

	bool rc = true;
	(void)reclaimed;
	(void)max_reclaimed;
	(void)num_reclaimed;
	io_data* data = new io_data();
	data->buf = buffer;
	data->iov.iov_base = buffer.data;
	data->iov.iov_len = buffer.dataLen;
	enqueue(&ring, data,reclaimed,max_reclaimed,num_reclaimed,false, m_fd);
	return rc;
}
bool FileUringIO::read(uint8_t* buf, size_t len)
{
	auto actual = (size_t)::read(m_fd, buf, len);
	if(actual < len)
		spdlog::error("read fewer bytes {} than expected number of bytes {}.", actual, len);

	return actual == len;
}
bool FileUringIO::seek(int64_t pos)
{
	return lseek(m_fd, pos, SEEK_SET) == pos;
}

#endif
