/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>
#include <exception>

namespace grk
{

class Codec
{
public:
  explicit Codec(grk::IStream* stream)
      : compressor_(nullptr), decompressor_(nullptr), stream_(stream), stop_worker_(false),
        thread_started_(false)
  {
    obj.wrapper = new GrkObjectWrapperImpl<Codec>(this);
  }

  ~Codec()
  {
    stopWorkerThread();
    delete compressor_;
    delete decompressor_;
  }

  static Codec* getImpl(grk_object* codec)
  {
    return ((GrkObjectWrapperImpl<Codec>*)codec->wrapper)->getWrappee();
  }

  grk_object* getWrapper()
  {
    return &obj;
  }

  std::future<bool> queueDecompressTile(uint16_t tile_index)
  {
    startWorkerThreadIfNeeded();

    std::promise<bool> promise;
    std::future<bool> future = promise.get_future();

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      task_queue_.push(TileTask{tile_index, std::move(promise)});
    }
    queue_cv_.notify_one(); // Notify the worker thread

    return future;
  }
  grk_object obj;
  ICompressor* compressor_;
  IDecompressor* decompressor_;
  std::unique_ptr<grk::IStream> stream_;

private:
  void startWorkerThreadIfNeeded()
  {
    std::lock_guard<std::mutex> lock(thread_start_mutex_);
    if(!thread_started_)
    {
      worker_ = std::thread(&Codec::workerThread, this);
      thread_started_ = true;
    }
  }

  void stopWorkerThread()
  {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      stop_worker_ = true;
      queue_cv_.notify_one();
    }

    if(worker_.joinable())
    {
      worker_.join();
    }
  }

  void workerThread()
  {
    while(true)
    {
      TileTask task;

      // Wait for tasks or stop signal
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this]() { return stop_worker_ || !task_queue_.empty(); });

        if(stop_worker_ && task_queue_.empty())
        {
          return; // Exit the thread
        }

        task = std::move(task_queue_.front());
        task_queue_.pop();
      }

      // Process the task
      try
      {
        bool success = decompressor_ ? decompressor_->decompressTile(task.tile_index) : false;
        task.promise.set_value(success);
      }
      catch(const std::exception& e)
      {
        task.promise.set_exception(std::current_exception());
      }
    }
  }

  struct TileTask
  {
    uint16_t tile_index;
    std::promise<bool> promise;
  };

  // Thread and task queue
  std::thread worker_;
  std::queue<TileTask> task_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::atomic<bool> stop_worker_; // Signal to stop the thread

  // Thread lazy initialization
  std::mutex thread_start_mutex_;
  bool thread_started_;
};

} // namespace grk