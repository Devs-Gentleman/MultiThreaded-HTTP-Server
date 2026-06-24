#include "file_cache.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <system_error>
#include <unordered_map>

namespace file_cache {
namespace fs = std::filesystem;

struct Entry {
    std::string body;
    fs::file_time_type mtime;
    std::list<std::string>::iterator lru_it;
};

class Cache {
public:
    bool enabled = true;
    std::size_t max_entries = 64;
    mutable std::shared_mutex mutex;
    std::unordered_map<std::string, Entry> entries;
    std::list<std::string> lru;
};

Cache& cache() {
    static Cache instance;
    return instance;
}

bool read_file_from_disk(const std::string& path, std::string* body, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) {
            *error = "file open failed";
        }
        return false;
    }

    std::string contents;
    file.seekg(0, std::ios::end);
    std::streampos end_pos = file.tellg();
    if (end_pos > 0) {
        contents.reserve(static_cast<std::size_t>(end_pos));
    }
    file.seekg(0, std::ios::beg);
    contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    *body = std::move(contents);
    return true;
}

void move_to_front(Cache& c, const std::string& path) {
    auto it = c.entries.find(path);
    if (it == c.entries.end()) {
        return;
    }
    c.lru.erase(it->second.lru_it);
    c.lru.push_front(path);
    it->second.lru_it = c.lru.begin();
}

void evict_if_needed(Cache& c) {
    while (c.entries.size() > c.max_entries && !c.lru.empty()) {
        const std::string& victim = c.lru.back();
        c.entries.erase(victim);
        c.lru.pop_back();
    }
}

void set_enabled(bool enabled) {
    Cache& c = cache();
    std::unique_lock<std::shared_mutex> lock(c.mutex);
    c.enabled = enabled;
}

bool is_enabled() {
    Cache& c = cache();
    std::shared_lock<std::shared_mutex> lock(c.mutex);
    return c.enabled;
}

void set_capacity(std::size_t capacity) {
    if (capacity == 0) {
        capacity = 1;
    }
    Cache& c = cache();
    std::unique_lock<std::shared_mutex> lock(c.mutex);
    c.max_entries = capacity;
    evict_if_needed(c);
}

std::size_t capacity() {
    Cache& c = cache();
    std::shared_lock<std::shared_mutex> lock(c.mutex);
    return c.max_entries;
}

bool read_file(const std::string& path, std::string* body, std::string* error) {
    Cache& c = cache();
    if (!body) {
        if (error) {
            *error = "body output missing";
        }
        return false;
    }

    std::error_code ec;
    fs::file_time_type current_mtime = fs::last_write_time(path, ec);
    if (ec) {
        if (error) {
            *error = "stat failed";
        }
        return false;
    }

    if (is_enabled()) {
        {
            std::shared_lock<std::shared_mutex> lock(c.mutex);
            auto it = c.entries.find(path);
            if (it != c.entries.end() && it->second.mtime == current_mtime) {
                std::string cached = it->second.body;
                lock.unlock();

                std::unique_lock<std::shared_mutex> unique_lock(c.mutex);
                auto mutable_it = c.entries.find(path);
                if (mutable_it != c.entries.end() && mutable_it->second.mtime == current_mtime) {
                    move_to_front(c, path);
                    *body = std::move(cached);
                    return true;
                }
            }
        }
    }

    std::string loaded;
    if (!read_file_from_disk(path, &loaded, error)) {
        return false;
    }

    if (is_enabled()) {
        std::unique_lock<std::shared_mutex> lock(c.mutex);
        auto& entry = c.entries[path];
        entry.body = loaded;
        entry.mtime = current_mtime;
        c.lru.remove(path);
        c.lru.push_front(path);
        entry.lru_it = c.lru.begin();
        evict_if_needed(c);
    }

    *body = std::move(loaded);
    return true;
}

}  // namespace file_cache
