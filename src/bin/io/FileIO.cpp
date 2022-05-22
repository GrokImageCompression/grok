#include "FileIO.h"

namespace iobench {

FileIO::FileIO(uint32_t threadId, bool flushOnClose) :
							numSimulatedWrites_(0),
							maxSimulatedWrites_(0),
							off_(0),
							reclaim_callback_(nullptr),
							reclaim_user_data_(nullptr),
							simulateWrite_(false),
							flushOnClose_(flushOnClose),
							threadId_(threadId)
{
}
void FileIO::setMaxPooledRequests(uint32_t maxRequests)
{
	maxSimulatedWrites_ = maxRequests;
}
void FileIO::registerReclaimCallback(io_callback reclaim_callback,
												 void* user_data)
{
	reclaim_callback_ = reclaim_callback;
	reclaim_user_data_ = user_data;
}

void FileIO::enableSimulateWrite(void){
	simulateWrite_ = true;
}

}
