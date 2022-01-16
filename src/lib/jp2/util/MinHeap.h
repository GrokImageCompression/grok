#pragma once

#include <mutex>

class MinHeapLocker {
public:
	MinHeapLocker(std::mutex &mut) :  lock(mut)
	{}
private:
	std::lock_guard<std::mutex> lock;
};

class MinHeapFakeLocker {
public:
	MinHeapFakeLocker(std::mutex &mut)
	{
		(void)mut;
	}
};

template <typename T> struct MinHeapComparator
{
	bool operator()(const T* a, const T* b) const
	{
		return a->getIndex() > b->getIndex();
	}
};

template <typename T, typename IT, typename L> class MinHeap
{
  public:
	MinHeap() : nextIndex(0) {}
	void push(T* val)
	{
		L locker(queue_mutex);
		queue.push(val);
	}
	T* pop(void)
	{
		L locker(queue_mutex);
		if(queue.empty())
			return nullptr;
		auto val = queue.top();
		if(val->getIndex() <= nextIndex)
		{
			queue.pop();
			if(val->getIndex() == nextIndex)
				nextIndex++;
			return val;
		}
		return nullptr;
	}
	size_t size(void)
	{
		return queue.size();
	}

  private:
	std::priority_queue<T*, std::vector<T*>, MinHeapComparator<T> > queue;
	std::mutex queue_mutex;
	IT nextIndex;
};

