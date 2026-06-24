#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(size_t num_workers);
    ~ThreadPool();

    // Enqueue a client fd to be processed by worker threads.
    void enqueue(int client_fd);

    // Stop workers and join threads. Safe to call multiple times.
    void stop();

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<int> jobs_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> stopping_{false};
};
