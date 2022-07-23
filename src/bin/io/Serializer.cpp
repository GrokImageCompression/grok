/*
 *    Copyright (C) 2022 Grok Image Compression Inc.
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
 */

#include "Serializer.h"

namespace io
{

static bool applicationReclaimCallback(uint32_t threadId, io_buf* buffer, void* io_user_data)
{
	(void)threadId;
	auto pool = (IBufferPool*)io_user_data;
	if(pool)
		pool->put((IOBuf*)buffer);

	return true;
}

Serializer::Serializer(uint32_t threadId, bool flushOnClose)
	: pool_(new BufferPool()), fileIO_(threadId, flushOnClose), threadId_(threadId)
{
	registerReclaimCallback(applicationReclaimCallback, pool_);
}
Serializer::~Serializer(void)
{
	close();
	delete pool_;
}
void Serializer::setMaxSimulatedWrites(uint64_t maxRequests)
{
	fileIO_.setMaxSimulatedWrites(maxRequests);
}
void Serializer::registerReclaimCallback(io_callback reclaim_callback, void* user_data)
{
	fileIO_.registerReclaimCallback(reclaim_callback, user_data);
}
IOBuf* Serializer::getPoolBuffer(uint64_t len)
{
	return pool_->get(len);
}
IBufferPool* Serializer::getPool(void)
{
	return pool_;
}
bool Serializer::attach(Serializer* parent)
{
	return fileIO_.attach(&parent->fileIO_);
}
bool Serializer::open(std::string name, std::string mode, bool asynch)
{
	return fileIO_.open(name, mode, asynch);
}
bool Serializer::close(void)
{
	return fileIO_.close();
}
bool Serializer::reopenAsBuffered(void)
{
	return fileIO_.reopenAsBuffered();
}
uint64_t Serializer::seek(int64_t off, int32_t whence)
{
	return fileIO_.seek(off, whence);
}
void Serializer::enableSimulateWrite(void)
{
	fileIO_.enableSimulateWrite();
}
uint64_t Serializer::write(uint64_t offset, IOBuf** buffers, uint32_t numBuffers)
{
	return fileIO_.write(offset, buffers, numBuffers);
}
uint64_t Serializer::write(uint8_t* buf, uint64_t bytes_total)
{
	return fileIO_.write(buf, bytes_total);
}

} // namespace io
