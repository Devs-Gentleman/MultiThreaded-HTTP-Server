#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <signal.h>
#include <atomic>
#include <cctype>

#include <system_error>

#include "http.h"
#include "static_files.h"
#include "thread_pool.h"
#include "logger.h"
#include "notes_store.h"
#include "json_util.h"
#include <chrono>
#include <sstream>
#include <vector>

namespace {
constexpr int kPort = 8080;    // TCP port number for the server
constexpr int kBacklog = 16;   // Max queued connections (listen backlog)
constexpr size_t kMaxRequestBytes = 32 * 1024;
constexpr const char* kWebRoot = "www";
std::mutex log_mutex;
volatile sig_atomic_t stop_requested = 0;
int g_server_fd = -1;
NotesStore g_notes_store;

struct Route {
    const char* method;
    const char* path;
    bool prefix_match;
};

const std::vector<Route> kApiRoutes = {
    {"GET", "/api/notes", false},
    {"POST", "/api/notes", false},
    {"DELETE", "/api/notes/", true},
};

bool send_all(int fd, const char* data, size_t size) {  // fd = file descriptor (socket handle)
    size_t total_sent = 0;
    while (total_sent < size) {
        // send() is the POSIX system call that sends bytes on a socket.
        ssize_t sent = ::send(fd, data + total_sent, size - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) {  // EINTR = Interrupted system call, try again.
                continue;
            }
            return false;
        }
        if (sent == 0) {
            return false;
        }
        total_sent += static_cast<size_t>(sent);  // sent is signed; we already know it's > 0.
    }
    return true;
}

void log_error(const std::string& event, const std::string& detail) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cerr << "level=error event=" << event << " thread=" << std::this_thread::get_id()
              << " detail=" << detail << "\n";
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

HttpResponse make_json_response(int status, const std::string& reason, const std::string& body) {
    HttpResponse response = make_text_response(status, reason, body);
    response.headers["Content-Type"] = "application/json";
    return response;
}

HttpResponse make_json_error_response(int status, const std::string& reason,
                                      const std::string& message) {
    return make_json_response(status, reason,
                              std::string("{\"error\":\"") + json_escape(message) + "\"}");
}

bool parse_json_string_field(const std::string& body, const std::string& field,
                             std::string* out, std::string* error) {
    if (!out) {
        if (error) {
            *error = "output missing";
        }
        return false;
    }

    std::string needle = std::string("\"") + field + "\"";
    size_t key_pos = body.find(needle);
    if (key_pos == std::string::npos) {
        if (error) {
            *error = "missing field: " + field;
        }
        return false;
    }

    size_t colon = body.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        if (error) {
            *error = "invalid json field";
        }
        return false;
    }

    size_t quote = body.find('"', colon + 1);
    if (quote == std::string::npos) {
        if (error) {
            *error = "invalid json string";
        }
        return false;
    }

    std::string value;
    bool escaping = false;
    for (size_t i = quote + 1; i < body.size(); ++i) {
        char ch = body[i];
        if (escaping) {
            switch (ch) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(ch); break;
            }
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (ch == '"') {
            *out = value;
            return true;
        }
        value.push_back(ch);
    }

    if (error) {
        *error = "unterminated json string";
    }
    return false;
}

bool parse_note_id_from_target(const std::string& target, int* out_id, std::string* error) {
    std::string prefix = "/api/notes/";
    if (!starts_with(target, prefix)) {
        if (error) {
            *error = "invalid api path";
        }
        return false;
    }

    std::string tail = target.substr(prefix.size());
    if (tail.empty() || tail.find('/') != std::string::npos) {
        if (error) {
            *error = "invalid note id";
        }
        return false;
    }

    try {
        size_t consumed = 0;
        int id = std::stoi(tail, &consumed, 10);
        if (consumed != tail.size() || id <= 0) {
            if (error) {
                *error = "invalid note id";
            }
            return false;
        }
        *out_id = id;
        return true;
    } catch (...) {
        if (error) {
            *error = "invalid note id";
        }
        return false;
    }
}

bool handle_notes_list(const HttpRequest&, HttpResponse* response, std::string* error) {
    if (!response) {
        if (error) {
            *error = "response output missing";
        }
        return false;
    }

    auto notes = g_notes_store.list_notes();
    *response = make_json_response(200, "OK", build_notes_json(notes));
    return true;
}

bool handle_notes_create(const HttpRequest& request, HttpResponse* response, std::string* error) {
    if (!response) {
        if (error) {
            *error = "response output missing";
        }
        return false;
    }

    std::string text;
    if (!parse_json_string_field(request.body, "text", &text, error)) {
        *response = make_json_error_response(400, "Bad Request", error ? *error : "bad json");
        return true;
    }
    if (text.empty()) {
        *response = make_json_error_response(400, "Bad Request", "text cannot be empty");
        return true;
    }

    Note note = g_notes_store.create_note(text);
    *response = make_json_response(201, "Created", build_note_json(note));
    response->headers["Location"] = "/api/notes/" + std::to_string(note.id);
    return true;
}

bool handle_notes_delete(const HttpRequest& request, HttpResponse* response, std::string* error) {
    if (!response) {
        if (error) {
            *error = "response output missing";
        }
        return false;
    }

    int id = 0;
    if (!parse_note_id_from_target(request.target, &id, error)) {
        *response = make_json_error_response(400, "Bad Request", error ? *error : "bad id");
        return true;
    }

    if (!g_notes_store.delete_note(id)) {
        *response = make_json_error_response(404, "Not Found", "note not found");
        return true;
    }

    HttpResponse ok;
    ok.status = 204;
    ok.reason = "No Content";
    ok.headers["Content-Type"] = "application/json";
    *response = ok;
    return true;
}

bool build_api_response(const HttpRequest& request, HttpResponse* response, std::string* error) {
    if (!starts_with(request.target, "/api/")) {
        return false;
    }

    if (request.target == "/api/notes") {
        if (request.method == "GET") {
            return handle_notes_list(request, response, error);
        }
        if (request.method == "POST") {
            return handle_notes_create(request, response, error);
        }

        HttpResponse method_not_allowed = make_json_error_response(405, "Method Not Allowed",
                                                                   "method not allowed");
        method_not_allowed.headers["Allow"] = "GET, POST";
        *response = method_not_allowed;
        return true;
    }

    if (starts_with(request.target, "/api/notes/")) {
        if (request.method == "DELETE") {
            return handle_notes_delete(request, response, error);
        }

        HttpResponse method_not_allowed = make_json_error_response(405, "Method Not Allowed",
                                                                   "method not allowed");
        method_not_allowed.headers["Allow"] = "DELETE";
        *response = method_not_allowed;
        return true;
    }

    *response = make_json_error_response(404, "Not Found", "api route not found");
    return true;
}

void handle_client_internal(int client_fd) {
    static const std::string bad_request_response =
        build_http_response(make_text_response(400, "Bad Request", "Bad Request\n"));

    constexpr int kMaxKeepAliveRequests = 100;
    constexpr int kKeepAliveTimeoutSec = 5;
    int request_count = 0;
    bool socket_timeout_set = false;

    while (true) {
        HttpRequest request;
        std::string parse_error;
        auto start_ts = std::chrono::steady_clock::now();
        if (!read_http_request(client_fd, kMaxRequestBytes, &request, &parse_error)) {
            log_error("request_failed", parse_error);
            send_all(client_fd, bad_request_response.data(), bad_request_response.size());
            std::ostringstream tid;
            tid << std::this_thread::get_id();
            server_logger::log_request("PARSE_ERROR", "-", 400, tid.str(), 0);
            break;  // close on parse error — connection state is unreliable
        }

        HttpResponse response;
        std::string handler_error;
        if (!build_api_response(request, &response, &handler_error)) {
            if (!build_static_response(request, kWebRoot, &response, &handler_error)) {
                log_error("handler_error", handler_error);
                response = make_text_response(500, "Internal Server Error",
                                              "Internal Server Error\n");
            }
        }

        // Only keep alive if the client asked and we haven't hit the per-connection limit
        bool response_keep_alive = request.keep_alive && request_count < kMaxKeepAliveRequests;

        std::string payload = build_http_response(response, response_keep_alive);
        if (!send_all(client_fd, payload.data(), payload.size())) {
            log_error("send_failed", "send_all returned false");
            break;
        }

        auto end_ts = std::chrono::steady_clock::now();
        long latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_ts - start_ts).count();
        std::ostringstream tid;
        tid << std::this_thread::get_id();
        server_logger::log_request(request.method.empty() ? "-" : request.method,
                                   request.target.empty() ? "-" : request.target,
                                   response.status, tid.str(), latency_ms);

        request_count++;

        if (!response_keep_alive) {
            break;
        }

        // Set a read timeout so idle kept-alive connections don't hold workers forever
        if (!socket_timeout_set) {
            struct timeval tv;
            tv.tv_sec = kKeepAliveTimeoutSec;
            tv.tv_usec = 0;
            if (::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
                log_error("setsockopt_failed", "SO_RCVTIMEO");
                break;
            }
            socket_timeout_set = true;
        }
    }

    ::close(client_fd);
}

}  // namespace

// Public wrapper used by worker threads in other translation units.
void handle_client(int client_fd) { return handle_client_internal(client_fd); }

void handle_sigint(int /*signum*/) {
    stop_requested = 1;
    if (g_server_fd >= 0) {
        ::close(g_server_fd);
        g_server_fd = -1;
    }
}


int main() {
    // socket() creates a TCP/IPv4 socket: AF_INET = IPv4, SOCK_STREAM = TCP.
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_error("socket_failed", std::strerror(errno));
        return 1;
    }

    // Make the server_fd visible to the signal handler so SIGINT can close it.
    g_server_fd = server_fd;
    ::signal(SIGINT, handle_sigint);

    int opt = 1;
    // setsockopt() = set socket option, SOL_SOCKET = socket-level option.
    // SO_REUSEADDR lets us rebind quickly after restart.
    if (::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_error("setsockopt_failed", "SO_REUSEADDR");
    }   
#ifdef SO_REUSEPORT
    // SO_REUSEPORT allows multiple sockets to bind the same port (if supported).
    if (::setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        log_error("setsockopt_failed", "SO_REUSEPORT");
    }
#endif
    
    sockaddr_in addr{};               // sockaddr_in = IPv4 socket address struct
    addr.sin_family = AF_INET;        // AF_INET = IPv4
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // INADDR_ANY = bind all interfaces
    addr.sin_port = htons(kPort);     // htons = host-to-network short (byte order)

    // bind() assigns the address/port to the socket.
    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        log_error("bind_failed", std::strerror(errno));
        ::close(server_fd);
        return 1;
    }

    // listen() turns the socket into a server that can accept connections.
    if (::listen(server_fd, kBacklog) < 0) {
        log_error("listen_failed", std::strerror(errno));
        ::close(server_fd);
        return 1;
    }

    std::cout << "Listening on port " << kPort << "...\n";
    // Initialize request logger (writes to server.log in project root)
    if (!server_logger::init_logger("server.log")) {
        log_error("logger_init_failed", "server.log");
    }

    unsigned int hc = std::thread::hardware_concurrency();
    size_t worker_count = hc == 0 ? 4 : static_cast<size_t>(hc);
    ThreadPool pool(worker_count);

    while (!stop_requested) {
        sockaddr_in client_addr{};  // Client IPv4 address info
        socklen_t client_len = sizeof(client_addr);  // socklen_t = length type for sockets
        // accept() waits for a client and returns a new socket for that connection.
        int client_fd = ::accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {  // Interrupted system call
                continue;
            }
            log_error("accept_failed", std::strerror(errno));
            if (stop_requested) break;
            continue;
        }
        pool.enqueue(client_fd);
    }

    // Stop the pool and join workers. The signal handler already closed server_fd.
    pool.stop();

    server_logger::shutdown_logger();

    if (server_fd >= 0) ::close(server_fd);
    return 0;
}
