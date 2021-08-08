#include "thread_pool.h"

ThreadPool::ThreadPool() {
  for (unsigned i = 0; i < kThreads; ++i)
    threads_[i] = std::thread([this] { Main(); });
}

void ThreadPool::Stop() {
  Submit(kThreads, std::function<void()>());
  for (std::thread& thread : threads_) thread.join();
}

void ThreadPool::Submit(std::function<void()> func) {
  {
    std::lock_guard lock(mutex_);
    queue_.push(std::move(func));
  }
  cv_.notify_one();
}

void ThreadPool::Submit(size_t n, std::function<void()> func) {
  if (n == 0) return;
  {
    std::lock_guard lock(mutex_);
    for (size_t i = 1; i < n; ++i) queue_.push(func);
    queue_.push(std::move(func));
  }
  if (n > 1)
    cv_.notify_all();
  else
    cv_.notify_one();
}

void ThreadPool::Submit(std::span<std::function<void()>> funcs) {
  if (funcs.empty()) return;
  for (std::lock_guard lock(mutex_); auto& func : funcs)
    queue_.push(std::move(func));
  if (funcs.size() > 1)
    cv_.notify_all();
  else
    cv_.notify_one();
}

void ThreadPool::Main() {
  for (;;) {
    std::function<void()> func;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return !queue_.empty(); });
      func = queue_.front();
      queue_.pop();
    }
    if (!func) break;
    func();
  }
}
