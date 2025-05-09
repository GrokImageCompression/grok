#pragma once

#include <mutex>
#include <queue>

namespace grk
{
class MinHeapLocker
{
public:
  MinHeapLocker(std::mutex& mut) : lock(mut) {}

private:
  std::lock_guard<std::mutex> lock;
};

class MinHeapFakeLocker
{
public:
  MinHeapFakeLocker([[maybe_unused]] std::mutex& mut) {}
};

template<typename T>
struct MinHeapComparator
{
  bool operator()(const T a, const T b) const
  {
    return a.getIndex() > b.getIndex();
  }
};

template<typename T, typename IT, typename L>
class MinHeap
{
public:
  MinHeap() : nextIndex(0) {}
  void push(T val)
  {
    L locker(queue_mutex);
    queue.push(val);
  }
  bool pop(T& val)
  {
    L locker(queue_mutex);
    if(queue.empty() || queue.top().getIndex() != nextIndex)
      return false;
    val = queue.top();
    queue.pop();
    nextIndex++;
    return true;
  }
  size_t size(void)
  {
    return queue.size();
  }

private:
  std::priority_queue<T, std::vector<T>, MinHeapComparator<T>> queue;
  std::mutex queue_mutex;
  IT nextIndex;
};

template<typename T>
struct MinHeapPtrComparator
{
  bool operator()(const T* a, const T* b) const
  {
    return a->getIndex() > b->getIndex();
  }
};

template<typename T, typename IT, typename L>
class MinHeapPtr
{
public:
  MinHeapPtr() : nextIndex(0) {}
  void push(T* val)
  {
    L locker(queue_mutex);
    queue.push(val);
  }
  T* pop(void)
  {
    L locker(queue_mutex);
    if(queue.empty() || queue.top()->getIndex() != nextIndex)
      return nullptr;
    auto val = queue.top();
    queue.pop();
    nextIndex++;
    return val;
  }
  size_t size(void)
  {
    return queue.size();
  }

private:
  std::priority_queue<T*, std::vector<T*>, MinHeapPtrComparator<T>> queue;
  std::mutex queue_mutex;
  IT nextIndex;
};

} // namespace grk
