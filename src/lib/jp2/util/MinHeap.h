#pragma once

#include <mutex>

template <typename T> struct MinHeapComparator
{
	bool operator()(const T* a, const T* b) const
	{
		return a->getIndex() > b->getIndex();
	}
};

template <typename T, typename IT> class MinHeap
{
  public:
	MinHeap() : nextIndex(0) {}
	void push(T* val)
	{
		std::lock_guard<std::mutex> lock(queue_mutex);
		queue.push(val);
	}
	T* pop(void)
	{
		std::lock_guard<std::mutex> lock(queue_mutex);
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
	bool empty(void)
	{
		std::lock_guard<std::mutex> lock(queue_mutex);
		return queue.empty();
	}

  private:
	std::priority_queue<T*, std::vector<T*>, MinHeapComparator<T> > queue;
	std::mutex queue_mutex;
	IT nextIndex;
};

