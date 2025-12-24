/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <mutex>
#include <queue>
#include <vector>
#include <concepts>

namespace grk
{

#include <optional>

/**
 * @brief A simple non-thread-safe min-heap for tracking contiguous sequences of size_t indices.
 */
template<typename T>
class SimpleHeap
{
public:
  /**
   * @brief Pushes an index onto the heap.
   * @param index The index to add.
   */
  void push(T index)
  {
    queue.push(index);
  }

  /**
   * @brief Pushes an index and returns the greatest integer at the end of the contiguous sequence.
   * @param index The index to push.
   * @return The greatest consecutive index in the sequence starting from the initial start,
   *         or std::nullopt if no contiguous sequence exists.
   */
  std::optional<T> push_and_pop(T index)
  {
    queue.push(index);

    // While the top of the heap matches the expected next index, pop it and extend the sequence
    while(!queue.empty() && queue.top() == start)
    {
      queue.pop();
      start++;
    }

    // Return the greatest index in the contiguous sequence
    // If start is still 0, no sequence has been established
    return start > 0 ? std::optional<T>(start - 1) : std::nullopt;
  }

  /**
   * @brief Returns the current size of the heap.
   * @return Number of indices in the heap.
   */
  size_t size() const
  {
    return queue.size();
  }

private:
  std::priority_queue<T, std::vector<T>, std::greater<T>> queue; ///< Min-heap of indices.
  T start = 0; ///< Tracks the next expected index in the sequence.
};

/**
 * @brief RAII locker for synchronizing access to a min-heap with a real mutex.
 */
class MinHeapLocker
{
public:
  explicit MinHeapLocker(std::mutex& mut) : lock(mut) {}

private:
  std::lock_guard<std::mutex> lock;
};

/**
 * @brief No-op locker for single-threaded or unsynchronized min-heap access.
 */
class MinHeapFakeLocker
{
public:
  explicit MinHeapFakeLocker([[maybe_unused]] std::mutex& mut) {}
};

/**
 * @concept HasGetIndex
 * @brief Concept ensuring a type T has a getIndex() method returning an integral type.
 */
template<typename T>
concept HasGetIndex = requires(T t) {
  { t.getIndex() } -> std::integral;
};

/**
 * @brief Comparator for min-heap ordering based on getIndex() (value version).
 * @tparam T Type stored in the heap, must satisfy HasGetIndex.
 */
template<typename T>
  requires HasGetIndex<T>
struct MinHeapComparator
{
  bool operator()(const T& a, const T& b) const
  {
    return a.getIndex() > b.getIndex();
  }
};

/**
 * @brief Base class providing common functionality for min-heap implementations.
 * @tparam IT Integral type used for tracking the starting index.
 */
template<typename IT>
class MinHeapBase
{
protected:
  MinHeapBase() : start(0) {}
  mutable std::mutex queue_mutex; ///< Mutex for thread-safe access.
  IT start; ///< Tracks the next expected index for sequential popping.
};

/**
 * @brief Thread-safe min-heap for values with customizable locking.
 * @tparam T Type stored in the heap, must satisfy HasGetIndex.
 * @tparam IT Integral type for indexing (e.g., int, size_t).
 * @tparam L Locker type (e.g., MinHeapLocker or MinHeapFakeLocker).
 */
template<typename T, typename IT, typename L>
  requires HasGetIndex<T> && std::integral<IT>
class MinHeap : protected MinHeapBase<IT>
{
public:
  using MinHeapBase<IT>::queue_mutex;
  using MinHeapBase<IT>::start;

  /**
   * @brief Pushes a value onto the heap.
   * @param val The value to add.
   */
  void push(const T& val)
  {
    L locker(queue_mutex);
    queue.push(val);
  }

  /**
   * @brief Pushes a value and pops all consecutive elements starting from 'start'.
   * @param val The value to push.
   * @return Vector of popped elements in index order.
   */
  std::vector<T> push_and_pop(const T& val)
  {
    L locker(queue_mutex);
    queue.push(val);
    std::vector<T> inSequence;
    while(!queue.empty() && queue.top().getIndex() == start)
    {
      inSequence.push_back(queue.top());
      queue.pop();
      start++;
    }
    return inSequence;
  }

  /**
   * @brief Returns the current size of the heap.
   * @return Number of elements in the heap.
   */
  size_t size() const
  {
    std::lock_guard<std::mutex> locker(queue_mutex);
    return queue.size();
  }

private:
  std::priority_queue<T, std::vector<T>, MinHeapComparator<T>> queue; ///< Underlying min-heap.
};

/**
 * @brief Comparator for min-heap ordering based on getIndex() (pointer version).
 * @tparam T Type pointed to, must satisfy HasGetIndex.
 */
template<typename T>
  requires HasGetIndex<T>
struct MinHeapPtrComparator
{
  bool operator()(const T* a, const T* b) const
  {
    return a->getIndex() > b->getIndex();
  }
};

/**
 * @brief Thread-safe min-heap for pointers with customizable locking.
 * @tparam T Type pointed to, must satisfy HasGetIndex.
 * @tparam IT Integral type for indexing (e.g., int, size_t).
 * @tparam L Locker type (e.g., MinHeapLocker or MinHeapFakeLocker).
 */
template<typename T, typename IT, typename L>
  requires HasGetIndex<T> && std::integral<IT>
class MinHeapPtr : protected MinHeapBase<IT>
{
public:
  using MinHeapBase<IT>::queue_mutex;
  using MinHeapBase<IT>::start;

  /**
   * @brief Pushes a pointer onto the heap.
   * @param val Pointer to the value to add (ownership not taken).
   */
  void push(T* val)
  {
    L locker(queue_mutex);
    queue.push(val);
  }

  /**
   * @brief Optionally pushes a pointer and pops all consecutive elements starting from 'start'.
   * @param val Pointer to push (nullptr means no push).
   * @return Vector of popped pointers in index order (ownership transferred to caller).
   */
  std::vector<T*> pop(T* val)
  {
    L locker(queue_mutex);
    if(val)
      queue.push(val);
    std::vector<T*> inSequence;
    while(!queue.empty() && queue.top()->getIndex() == start)
    {
      inSequence.push_back(queue.top());
      queue.pop();
      start++;
    }
    return inSequence;
  }

  /**
   * @brief Returns the current size of the heap.
   * @return Number of pointers in the heap.
   */
  size_t size() const
  {
    std::lock_guard<std::mutex> locker(queue_mutex);
    return queue.size();
  }

private:
  std::priority_queue<T*, std::vector<T*>, MinHeapPtrComparator<T>> queue; ///< Underlying min-heap.
};

} // namespace grk