#include "UringSerializer.h"

UringSerializer::UringSerializer(void) :
							reclaimed(nullptr),
							max_reclaimed(0),
							num_reclaimed(nullptr),
							maxPixelRequests(0),
							numPixelRequests(0),
#ifdef GROK_HAVE_URING
							active_(true),
#else
							active_(false),
#endif
							off_(0)
{}

#ifdef GROK_HAVE_URING
bool UringSerializer::write(void){
	if (!active_)
		return false;
	scheduled.offset = off_;
	uring.write(scheduled,reclaimed,max_reclaimed,num_reclaimed);
	off_ += scheduled.dataLen;
	if (scheduled.pooled)
		numPixelRequests++;
	if (numPixelRequests == maxPixelRequests){
		uring.close();
		active_ = false;
	}
	// clear scheduled
	scheduled = GrkSerializeBuf();
	num_reclaimed = nullptr;
	reclaimed = nullptr;
	max_reclaimed = 0;

	return true;
}
#endif

bool UringSerializer::isActive(void){
	return active_;
}
uint64_t UringSerializer::getAsynchFileLength(void){
	return off_;
}
