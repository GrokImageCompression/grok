#include "Serializer.h"
#include "common.h"

Serializer::Serializer(void) :
							reclaimed_(nullptr),
							max_reclaimed_(0),
							num_reclaimed_(nullptr),
							numPixelRequests_(0),
							maxPixelRequests_(0),
#ifndef _WIN32
							fd_(0),
#endif
							asynchActive_(false),
							off_(0)
{}
void Serializer::init(grk_image *image){
	maxPixelRequests_ = ((image->y1 - image->y0) + image->rowsPerStrip - 1) / image->rowsPerStrip;
}
#ifndef _WIN32
int Serializer::getFd(void){
	return fd_;
}
int Serializer::getMode(const char* mode)
{
	int m = -1;

	switch (mode[0]) {
	case 'r':
		m = O_RDONLY;
		if (mode[1] == '+')
			m = O_RDWR;
		break;
	case 'w':
	case 'a':
		m = O_RDWR|O_CREAT;
		if (mode[0] == 'w')
			m |= O_TRUNC;
		break;
	default:
		spdlog::error("Bad mode {}", mode);
		break;
	}
	return (m);
}

bool Serializer::open(const char* name, const char* mode, bool readOp){
	(void)readOp;
	int m = getMode(mode);
	if (m == -1)
		return false;

	int fd = ::open(name, m, 0666);
	if (fd < 0) {
		if (errno > 0 && strerror(errno) != NULL )
			spdlog::error("{}: {}", name, strerror(errno) );
		else
			spdlog::error("Cannot open {}", name);
		return false;
	}

#ifdef GROK_HAVE_URING
	if (!uring.attach(name, mode, fd))
		return false;
	asynchActive_ = true;
#endif

	fd_ = fd;

	return true;
}
bool Serializer::close(void){
#ifdef GROK_HAVE_URING
		uring.close();
#endif

	return ::close(fd_) == 0;
}
#ifdef GROK_HAVE_URING
bool Serializer::write(uint8_t *buf, uint64_t size){
	if (!asynchActive_)
		return false;
	scheduled_.data = buf;
	scheduled_.dataLen = size;
	scheduled_.offset = off_;
	uring.write(scheduled_,reclaimed_,max_reclaimed_,num_reclaimed_);
	off_ += scheduled_.dataLen;
	if (scheduled_.pooled)
		numPixelRequests_++;
	if (numPixelRequests_ == maxPixelRequests_){
		uring.close();
		asynchActive_ = false;
	}
	clear();

	return true;
}
#endif
#endif
void Serializer::initPixelRequest(grk_serialize_buf* reclaimed,
									uint32_t max_reclaimed,
									uint32_t *num_reclaimed){
#ifdef GROK_HAVE_URING
	scheduled_.pooled = true;
#endif
	reclaimed_ = reclaimed;
	max_reclaimed_ = max_reclaimed;
	num_reclaimed_ = num_reclaimed;
}
uint32_t Serializer::incrementPixelRequest(void){
// write method will increment numPixelRequests if uring is enabled
#ifndef GROK_HAVE_URING
	numPixelRequests_++;
#endif

	return numPixelRequests_;
}

uint32_t Serializer::getNumPixelRequests(void){
	return numPixelRequests_;
}
uint64_t Serializer::getOffset(void){
	return off_;
}
bool Serializer::allPixelRequestsComplete(void){
	return numPixelRequests_ == maxPixelRequests_;
}
void Serializer::clear(void){
#ifdef GROK_HAVE_URING
	scheduled_ = GrkSerializeBuf();
#endif
	num_reclaimed_ = nullptr;
	reclaimed_ = nullptr;
	max_reclaimed_ = 0;
}
bool Serializer::isAsynchActive(void){
	return asynchActive_;
}
uint64_t Serializer::getAsynchFileLength(void){
	return off_;
}
