#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <span>
#include <thread>

constexpr unsigned kThreads = 8;

class ThreadPool {
 public:
  ThreadPool();

  ~ThreadPool() { Stop(); }

  // 提交单个任务
  void Submit(std::function<void()> func);

  // 提交多个相同任务
  void Submit(size_t n, std::function<void()> func);

  // 提交一系列任务
  void Submit(std::span<std::function<void()>> funcs);

  // 提交一系列任务，每一个都用func执行，并等待执行完成
  template <typename T, typename Callback>
  void SyncRunSpan(std::span<T> data, Callback func) {
    unsigned n = data.size();
    if (n == 0) return;

    std::condition_variable cv;
    std::mutex mutex;
    unsigned num = std::min(n, kThreads);

    std::atomic<unsigned> head{0};

    Submit(num, [&] {
      unsigned k;
      while ((k = head.fetch_add(1)) < n) func(data[k]);

      {
        std::lock_guard lock(mutex);
        --num;
      }
      cv.notify_one();
    });

    {
      std::unique_lock lock(mutex);
      cv.wait(lock, [&] { return num == 0; });
    }
  }

 private:
  void Main();
  void Stop();

 private:
  std::thread threads_[kThreads];
  std::queue<std::function<void()>> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
};
