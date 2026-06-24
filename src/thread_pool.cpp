#include "thread_pool.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <unistd.h>

// Forward declaration of handle_client implemented in main.cpp
// We declare it here so workers can call it. main.cpp defines it.
extern void handle_client(int client_fd);

ThreadPool::ThreadPool(size_t num_workers) {
    if (num_workers == 0) {
        num_workers = 4;
    }
    workers_.reserve(num_workers);
    for (size_t i = 0; i < num_workers; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::enqueue(int client_fd) {
    if (stopping_.load()) {
        ::close(client_fd);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mu_);
        jobs_.push(client_fd);
    }
    cv_.notify_one();
}

void ThreadPool::stop() {
    bool expected = false;
    if (!stopping_.compare_exchange_strong(expected, true)) {
        // already stopping
        return;
    }
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
    // Drain remaining jobs and close fds
    while (!jobs_.empty()) {
        int fd = jobs_.front(); jobs_.pop();
        ::close(fd);
    }
}

void ThreadPool::worker_loop() {
    while (true) {
        int fd = -1;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return stopping_.load() || !jobs_.empty(); });
            if (stopping_.load() && jobs_.empty()) return;
            if (!jobs_.empty()) {
                fd = jobs_.front(); jobs_.pop();
            } else {
                continue;
            }
        }

        if (fd >= 0) {
            try {
                handle_client(fd);
            } catch (const std::exception& ex) {
                std::cerr << "worker caught exception: " << ex.what() << "\n";
                ::close(fd);
            }
        }
    }
}
