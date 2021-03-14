#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <mingw.invoke.h>
#include <mingw.future.h>
#include <ratio>
#include <jthread.hpp>
#include <utility>

#include <function2/function2.hpp>
#include <riften/deque.hpp>
#include "semaphore.hpp"

namespace riften {

// Bind F and args... into nullary lambda
template <typename F, typename... Args> auto bind(F &&f, Args &&...arg) {
    return [f = std::forward<F>(f), ... arg = std::forward<Args>(arg)]() mutable -> decltype(auto) {
        return std::invoke(std::forward<F>(f), std::forward<Args>(arg)...);
    };
}

// Like std::packaged_task<R() &&>, but garantees no type-erasure.
template <std::invocable F> class NullaryOneShot {
  public:
    using result_type = std::invoke_result_t<F>;

    NullaryOneShot(F &&fn) : _fn(std::forward<F>(fn)) {}

    std::future<result_type> get_future() { return _promise.get_future(); }

    void operator()() && {
        if constexpr (!std::is_same_v<void, result_type>) {
            _promise.set_value(std::invoke(std::forward<F>(_fn)));
        } else {
            std::invoke(std::forward<F>(_fn));
            _promise.set_value();
        }
    }

  private:
    std::promise<result_type> _promise;
    F _fn;
};

template <typename F> NullaryOneShot(F &&) -> NullaryOneShot<F>;

class Threadpool {
  public:
    int thread_number(std::thread::id id){
    	if (id_map.find(id) != id_map.end())
    		return (int)id_map[id];
    	return -1;
    }
    explicit Threadpool(std::size_t threads = std::thread::hardware_concurrency()) : _deques(threads) {
        for (std::size_t i = 0; i < threads; ++i) {
            _threads.emplace_back([&, id = i](std::stop_token tok) {
                while (!tok.stop_requested() || _in_flight.load(std::memory_order_acquire) > 0) {
                    // Wait to be signalled
                    _deques[id].sem.acquire();

                    auto try_do_task_at = [&](std::size_t j) {
                        if (std::optional one_shot = _deques[j].tasks.steal()) {
                            // https://www.boost.org/doc/libs/1_75_0/doc/html/atomic/usage_examples.html
                            if (_in_flight.fetch_sub(1, std::memory_order_release) == 1) {
                                std::atomic_thread_fence(std::memory_order_acquire);
                            }
                            std::invoke(std::move(*one_shot));
                        }
                    };

                    // Work until the task are done
                    while (_in_flight.load(std::memory_order_acquire) != 0) {
                        for (size_t i = 0; i < 10000; i++) {
                            try_do_task_at(id);
                        }
                        for (size_t i = 1; i < _deques.size(); i++) {
                            try_do_task_at((i + id) % _deques.size());
                        }
                    }
                }
            });
        }
    }

    ~Threadpool() {
        for (auto &t : _threads) {
            t.request_stop();
        }
        for (auto &d : _deques) {
            d.sem.release();
        }
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker: workers)
        worker.join();
    }

    template <typename F, typename... Args> auto enqueue(F &&f, Args &&...args) {
        //
        auto task = NullaryOneShot(bind(std::forward<F>(f), std::forward<Args>(args)...));

        auto future = task.get_future();

        _deques[count % _deques.size()].tasks.emplace(std::move(task));

        _in_flight.fetch_add(1, std::memory_order_relaxed);

        _deques[count % _deques.size()].sem.release();

        count++;

        return future;
    }

  //private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;

    struct NamesPair {
        Semaphore sem{0};
        Deque<fu2::unique_function<void() &&>> tasks;
    };

    std::atomic<std::int64_t> _in_flight;
    std::size_t count = 0;
    std::vector<NamesPair> _deques;
    std::vector<std::jthread> _threads;

    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
    std::map<std::thread::id, size_t> id_map;
};

}  // namespace riften
