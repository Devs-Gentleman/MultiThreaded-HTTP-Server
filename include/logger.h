#pragma once

#include <string>

namespace server_logger {
// Initialize logger; returns false on failure.
bool init_logger(const std::string& path);

// Shut down logger and flush file.
void shutdown_logger();

// Log a single request. Thread-safe.
void log_request(const std::string& method, const std::string& target, int status,
                 const std::string& thread_id, long latency_ms);

// Get recent in-memory entries as a JSON-ish string (for diagnostics).
std::string recent_entries();
}
