#include "Serializer.h"
#include "common.h"
#define IO_MAX 2147483647U

Serializer::Serializer(void)
	:
#ifdef 	GROK_HAVE_URING
	  reclaimed_(nullptr),
	  max_reclaimed_(0),
	  num_reclaimed_(nullptr),
#endif
	  numPixelRequests_(0),
	  maxPixelRequests_(0),
#ifndef _WIN32
	  fd_(0),
#endif
	  asynchActive_(false), off_(0)
{}
void Serializer::init(grk_image* image)
{
	maxPixelRequests_ = ((image->y1 - image->y0) + image->rowsPerStrip - 1) / image->rowsPerStrip;
}
#ifdef _WIN32

bool Serializer::open(std::string name, std::string mode){
	return fileStreamIO.open(name,mode);
}
bool Serializer::close(void){
	return fileStreamIO.close();
}
bool Serializer::write(uint8_t* buf, size_t size){
	return fileStreamIO.write(buf,0,size,size,false);
}
uint64_t Serializer::seek(int64_t off, int whence){
	return fileStreamIO.seek(off,whence);
}

#else
int Serializer::getFd(void)
{
	return fd_;
}
int Serializer::getMode(std::string mode)
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
		case 'a':
			m = O_RDWR | O_CREAT;
			if(mode[0] == 'w')
				m |= O_TRUNC;
			break;
		default:
			spdlog::error("Bad mode {}", mode);
			break;
	}
	return (m);
}

bool Serializer::open(std::string name, std::string mode)
{
	bool useStdio = grk::useStdio(name);
	bool doRead = mode[0] == -'r';
	int fd = 0;
	if(useStdio) {
		fd = doRead ? STDIN_FILENO : STDOUT_FILENO;
	} else {
		int m = getMode(mode);
		if(m == -1)
			return false;

		fd = ::open(name.c_str(), m, 0666);
		if(fd < 0)
		{
			if(errno > 0 && strerror(errno) != NULL)
				spdlog::error("{}: {}", name, strerror(errno));
			else
				spdlog::error("Cannot open {}", name);
			return false;
		}
	}
#ifdef GROK_HAVE_URING
	if(!uring.attach(name, mode, fd))
		return false;
	asynchActive_ = true;
#endif
	fd_ = fd;

	return true;
}
bool Serializer::close(void)
{
#ifdef GROK_HAVE_URING
	uring.close();
#endif

	return ::close(fd_) == 0;
}
uint64_t Serializer::seek(int64_t off, int32_t whence){
	if (asynchActive_)
		return off_;
	off_t rc = lseek(getFd(), off, whence);
	if (rc == (off_t)-1){
		if(strerror(errno) != NULL)
			spdlog::error("{}", strerror(errno));
		else
			spdlog::error("I/O error");
		return (uint64_t)-1;
	}

	return (uint64_t)rc;
}
bool Serializer::write(uint8_t* buf, size_t bytes_total)
{
#ifdef GROK_HAVE_URING
	if(asynchActive_) {
		scheduled_.data = buf;
		scheduled_.dataLen = bytes_total;
		scheduled_.offset = off_;
		uring.write(scheduled_, reclaimed_, max_reclaimed_, num_reclaimed_);
		off_ += scheduled_.dataLen;
		if(scheduled_.pooled)
			numPixelRequests_++;
		if(numPixelRequests_ == maxPixelRequests_)
		{
			uring.close();
			asynchActive_ = false;
		}
		// clear
		scheduled_ = GrkSerializeBuf();
		num_reclaimed_ = nullptr;
		reclaimed_ = nullptr;
		max_reclaimed_ = 0;

		return true;
	}
#endif
	ssize_t count = 1;
	size_t bytes_written = 0;
	for(; bytes_written < bytes_total; bytes_written += (size_t)count)
	{
		const char* buf_offset = (char*)buf + bytes_written;
		size_t io_size = (size_t)(bytes_total - bytes_written);
		if(io_size > IO_MAX)
			io_size = IO_MAX;
		count = ::write(fd_, buf_offset, io_size);
		if(count <= 0)
			break;
	}

	return (count != -1);
}
#endif // #ifndef _WIN32

#ifdef GROK_HAVE_URING
void Serializer::initPixelRequest(grk_serialize_buf* reclaimed, uint32_t max_reclaimed,
								  uint32_t* num_reclaimed)
{
	scheduled_.pooled = true;
	reclaimed_ = reclaimed;
	max_reclaimed_ = max_reclaimed;
	num_reclaimed_ = num_reclaimed;
}
#else
void Serializer::incrementPixelRequest(void)
{
    // write method will take care of incrementing numPixelRequests if uring is enabled
	numPixelRequests_++;
}
#endif
uint32_t Serializer::getNumPixelRequests(void)
{
	return numPixelRequests_;
}
uint64_t Serializer::getOffset(void)
{
	return off_;
}
bool Serializer::allPixelRequestsComplete(void)
{
	return numPixelRequests_ == maxPixelRequests_;
}
