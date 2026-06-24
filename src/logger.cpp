#include "logger.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace server_logger {

struct Entry {
    std::string timestamp;
    std::string method;
    std::string target;
    int status;
    std::string thread_id;
    long latency_ms;
};

static std::mutex mu;
static std::ofstream log_file;
static std::vector<Entry> ring;
static const size_t kMaxEntries = 1000;
static bool initialized = false;

static std::string now_iso8601() {
    using namespace std::chrono;
    auto t = system_clock::now();
    std::time_t tt = system_clock::to_time_t(t);
    auto ms = duration_cast<milliseconds>(t.time_since_epoch()) % 1000;
    std::tm tm;
#if defined(__unix__)
    localtime_r(&tt, &tm);
#else
    tm = *std::localtime(&tt);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
       << ms.count();
    return ss.str();
}

bool init_logger(const std::string& path) {
    std::lock_guard<std::mutex> lock(mu);
    if (initialized) return true;
    log_file.open(path, std::ios::app | std::ios::out);
    if (!log_file.is_open()) return false;
    ring.reserve(kMaxEntries);
    initialized = true;
    return true;
}

void shutdown_logger() {
    std::lock_guard<std::mutex> lock(mu);
    if (!initialized) return;
    log_file.flush();
    log_file.close();
    ring.clear();
    initialized = false;
}

void log_request(const std::string& method, const std::string& target, int status,
                 const std::string& thread_id, long latency_ms) {
    std::lock_guard<std::mutex> lock(mu);
    if (!initialized) return;
    Entry e{now_iso8601(), method, target, status, thread_id, latency_ms};
    // Append to ring buffer
    if (ring.size() == kMaxEntries) {
        // simple rotate: erase first
        ring.erase(ring.begin());
    }
    ring.push_back(e);

    // Write a structured log line
    // Example: 2026-06-22T12:00:00.123 level=info method=GET path=/ status=200 thread=1 latency_ms=5
    log_file << e.timestamp << " level=info"
             << " method=" << e.method
             << " path=" << e.target
             << " status=" << e.status
             << " thread=" << e.thread_id
             << " latency_ms=" << e.latency_ms << '\n';
    log_file.flush();
}

std::string recent_entries() {
    std::lock_guard<std::mutex> lock(mu);
    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (const auto& e : ring) {
        if (!first) ss << ", ";
        first = false;
        ss << "{\"ts\":\"" << e.timestamp << "\",\"m\":\"" << e.method
           << "\",\"p\":\"" << e.target << "\",\"s\":" << e.status
           << ",\"t\":\"" << e.thread_id << "\",\"l\":" << e.latency_ms << "}";
    }
    ss << "]";
    return ss.str();
}

}  // namespace server_logger
