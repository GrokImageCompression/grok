#pragma once

#include <cstdint>
#include <atomic>

namespace iobench {

class RefCounted {
friend class RefReaper;
public:
	RefCounted(void) : refCount_(1)
	{}
	uint32_t ref(void){
	   return ++refCount_;
	}
protected:
	virtual ~RefCounted() = default;
private:
	uint32_t unref(void) {
		assert(refCount_ > 0);
		return --refCount_;
	}
	std::atomic<uint32_t> refCount_;
};

class RefReaper{
public:
	static void unref(RefCounted *refCounted){
		if (!refCounted)
			return;
		if (refCounted->unref() == 0)
			delete refCounted;
	}
};

}
