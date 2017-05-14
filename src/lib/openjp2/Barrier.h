#pragma once

#include <mutex>
#include <condition_variable>

namespace grk {

class Barrier {
public:
	explicit Barrier(std::size_t iCount) :
		mThreshold(iCount),
		mCount(iCount),
		mGeneration(0) {
	}

	void arrive_and_wait() {
		auto lGen = mGeneration;
		std::unique_lock<std::mutex> lLock{ mMutex };
		if (!--mCount) {
			mGeneration++;
			mCount = mThreshold;
			mCond.notify_all();
		}
		else {
			mCond.wait(lLock, [this, lGen] { return lGen != mGeneration; });
		}
	}

private:
	std::mutex mMutex;
	std::condition_variable mCond;
	size_t mThreshold;
	size_t mCount;
	size_t mGeneration;
};


}

