#include "http.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <exception>
#include <limits>
#include <sstream>
#include <sys/socket.h>

namespace {
constexpr size_t kReadChunkSize = 4096;

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

bool read_until_marker(int fd, std::string* buffer, size_t max_bytes, const std::string& marker,
                       std::string* error) {
    while (buffer->find(marker) == std::string::npos) {
        char chunk[kReadChunkSize];
        ssize_t received = ::recv(fd, chunk, sizeof(chunk), 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (error) {
                *error = std::string("recv failed: ") + std::strerror(errno);
            }
            return false;
        }
        if (received == 0) {
            if (error) {
                *error = "client closed connection";
            }
            return false;
        }
        buffer->append(chunk, static_cast<size_t>(received));
        if (buffer->size() > max_bytes) {
            if (error) {
                *error = "request too large";
            }
            return false;
        }
    }
    return true;
}

bool parse_request_line(const std::string& line, HttpRequest* out, std::string* error) {
    std::istringstream stream(line);
    if (!(stream >> out->method >> out->target >> out->version)) {
        if (error) {
            *error = "malformed request line";
        }
        return false;
    }
    return true;
}

bool parse_headers(std::istringstream& stream, HttpRequest* out, std::string* error) {
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            if (error) {
                *error = "malformed header line";
            }
            return false;
        }
        std::string key = to_lower_copy(trim(line.substr(0, colon)));
        std::string value = trim(line.substr(colon + 1));
        out->headers[key] = value;
    }
    return true;
}

bool parse_content_length(const HttpRequest& request, size_t* content_length, std::string* error) {
    auto it = request.headers.find("content-length");
    if (it == request.headers.end()) {
        *content_length = 0;
        return true;
    }
    try {
        size_t pos = 0;
        unsigned long long value = std::stoull(it->second, &pos, 10);
        if (pos != it->second.size()) {
            if (error) {
                *error = "invalid content-length";
            }
            return false;
        }
        if (value > std::numeric_limits<size_t>::max()) {
            if (error) {
                *error = "content-length out of range";
            }
            return false;
        }
        *content_length = static_cast<size_t>(value);
    } catch (const std::exception&) {
        if (error) {
            *error = "invalid content-length";
        }
        return false;
    }
    return true;
}
}  // namespace

bool read_http_request(int fd, size_t max_bytes, HttpRequest* out, std::string* error) {
    if (!out) {
        return false;
    }
    *out = HttpRequest{};

    std::string data;
    if (!read_until_marker(fd, &data, max_bytes, "\r\n\r\n", error)) {
        return false;
    }

    size_t header_end = data.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        if (error) {
            *error = "header terminator not found";
        }
        return false;
    }

    std::string header_block = data.substr(0, header_end);
    std::istringstream header_stream(header_block);
    std::string line;
    if (!std::getline(header_stream, line)) {
        if (error) {
            *error = "empty request";
        }
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (!parse_request_line(line, out, error)) {
        return false;
    }
    if (!parse_headers(header_stream, out, error)) {
        return false;
    }

    // Detect keep-alive from Connection header
    auto conn_it = out->headers.find("connection");
    if (conn_it != out->headers.end()) {
        std::string conn_val = to_lower_copy(conn_it->second);
        if (conn_val.find("keep-alive") != std::string::npos) {
            out->keep_alive = true;
        }
    }

    size_t content_length = 0;
    if (!parse_content_length(*out, &content_length, error)) {
        return false;
    }

    size_t body_start = header_end + 4;
    if (content_length > 0) {
        if (content_length > max_bytes - body_start) {
            if (error) {
                *error = "request too large";
            }
            return false;
        }
        size_t needed = body_start + content_length;
        while (data.size() < needed) {
            char chunk[kReadChunkSize];
            ssize_t received = ::recv(fd, chunk, sizeof(chunk), 0);
            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (error) {
                    *error = std::string("recv failed: ") + std::strerror(errno);
                }
                return false;
            }
            if (received == 0) {
                if (error) {
                    *error = "client closed connection";
                }
                return false;
            }
            data.append(chunk, static_cast<size_t>(received));
            if (data.size() > max_bytes) {
                if (error) {
                    *error = "request too large";
                }
                return false;
            }
        }
        out->body = data.substr(body_start, content_length);
    } else if (data.size() > body_start) {
        out->body = data.substr(body_start);
    }

    return true;
}

std::string build_http_response(const HttpResponse& response, bool keep_alive) {
    std::string output;
    output.reserve(response.body.size() + 128);

    output += "HTTP/1.1 ";
    output += std::to_string(response.status);
    output += " ";
    output += response.reason;
    output += "\r\n";

    bool has_content_type = false;
    for (const auto& header : response.headers) {
        std::string lower = to_lower_copy(header.first);
        if (lower == "content-length" || lower == "connection") {
            continue;
        }
        if (lower == "content-type") {
            has_content_type = true;
        }
        output += header.first;
        output += ": ";
        output += header.second;
        output += "\r\n";
    }

    if (!has_content_type) {
        output += "Content-Type: text/plain\r\n";
    }

    output += "Content-Length: ";
    output += std::to_string(response.body.size());
    output += "\r\n";
    output += "Connection: ";
    output += (keep_alive ? "keep-alive\r\n" : "close\r\n");
    if (keep_alive) {
        output += "Keep-Alive: timeout=5, max=100\r\n";
    }
    output += "\r\n";
    output += response.body;

    return output;
}

HttpResponse make_text_response(int status, const std::string& reason, const std::string& body) {
    HttpResponse response;
    response.status = status;
    response.reason = reason;
    response.body = body;
    response.headers["Content-Type"] = "text/plain";
    return response;
}
