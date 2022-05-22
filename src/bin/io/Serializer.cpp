#include "Serializer.h"

namespace iobench {

static bool applicationReclaimCallback(uint32_t threadId,
										io_buf *buffer,
										void* io_user_data)
{
	(void)threadId;
	auto pool = (IBufferPool*)io_user_data;
	if(pool)
		pool->put((IOBuf*)buffer);

	return true;
}

Serializer::Serializer(uint32_t threadId, bool flushOnClose) :
	  pool_(new BufferPool()),
	  fileIO_(threadId, flushOnClose),
	  threadId_(threadId)
{
	registerReclaimCallback(applicationReclaimCallback, pool_);
}
Serializer::~Serializer(void){
	close();
	delete pool_;
}
void Serializer::setMaxPooledRequests(uint32_t maxRequests)
{
	fileIO_.setMaxPooledRequests(maxRequests);
}
void Serializer::registerReclaimCallback(io_callback reclaim_callback,
												 void* user_data)
{
	fileIO_.registerReclaimCallback(reclaim_callback, user_data);
}
IOBuf* Serializer::getPoolBuffer(uint64_t len){
	return pool_->get(len);
}
IBufferPool* Serializer::getPool(void){
	return pool_;
}
bool Serializer::attach(Serializer *parent){
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
uint64_t Serializer::seek(int64_t off, int32_t whence)
{
	return fileIO_.seek(off, whence);
}
void Serializer::enableSimulateWrite(void){
	fileIO_.enableSimulateWrite();
}
uint64_t Serializer::write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers){
	return fileIO_.write(offset, buffers, numBuffers);
}
uint64_t Serializer::write(uint8_t* buf, uint64_t bytes_total)
{
	return fileIO_.write(buf, bytes_total);
}

}
