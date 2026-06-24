#include "static_files.h"
#include "file_cache.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {
std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string extract_path(const std::string& target) {
    size_t end = target.find_first_of("?#");
    if (end == std::string::npos) {
        return target;
    }
    return target.substr(0, end);
}

bool resolve_path(const std::string& target, const std::string& web_root, std::string* out_path,
                  std::string* error) {
    std::string path = extract_path(target);
    if (path.empty()) {
        path = "/";
    }
    bool ends_with_slash = !path.empty() && path.back() == '/';

    std::vector<std::string> segments;
    size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') {
            ++i;
        }
        if (i >= path.size()) {
            break;
        }
        size_t j = path.find('/', i);
        std::string segment = path.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (segment == ".") {
            i = (j == std::string::npos) ? path.size() : j + 1;
            continue;
        }
        if (segment == ".." || segment.find('\\') != std::string::npos) {
            if (error) {
                *error = "invalid path";
            }
            return false;
        }
        segments.push_back(segment);
        i = (j == std::string::npos) ? path.size() : j + 1;
    }

    std::string relative;
    for (const auto& segment : segments) {
        if (!relative.empty()) {
            relative += '/';
        }
        relative += segment;
    }
    if (relative.empty() || ends_with_slash) {
        if (!relative.empty()) {
            relative += '/';
        }
        relative += "index.html";
    }

    *out_path = web_root + "/" + relative;
    return true;
}

std::string get_extension(const std::string& path) {
    size_t slash = path.find_last_of('/');
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return std::string();
    }
    return to_lower_copy(path.substr(dot + 1));
}

std::string mime_type_for_path(const std::string& path) {
    static const std::unordered_map<std::string, std::string> kMimeTypes = {
        {"html", "text/html"},
        {"htm", "text/html"},
        {"css", "text/css"},
        {"js", "application/javascript"},
        {"json", "application/json"},
        {"txt", "text/plain"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"svg", "image/svg+xml"},
        {"ico", "image/x-icon"}
    };

    std::string ext = get_extension(path);
    auto it = kMimeTypes.find(ext);
    if (it != kMimeTypes.end()) {
        return it->second;
    }
    return "application/octet-stream";
}

HttpResponse make_not_found_response(const std::string& web_root) {
    HttpResponse response;
    response.status = 404;
    response.reason = "Not Found";

    std::string body;
    std::string cache_error;
    if (file_cache::read_file(web_root + "/404.html", &body, &cache_error)) {
        response.body = body;
        response.headers["Content-Type"] = "text/html";
        return response;
    }

    response = make_text_response(404, "Not Found", "Not Found\n");
    return response;
}
}  // namespace

bool build_static_response(const HttpRequest& request, const std::string& web_root,
                           HttpResponse* response, std::string* error) {
    if (!response) {
        if (error) {
            *error = "response output missing";
        }
        return false;
    }

    if (request.method != "GET") {
        *response = make_text_response(405, "Method Not Allowed", "Method Not Allowed\n");
        response->headers["Allow"] = "GET";
        return true;
    }

    std::string resolved_path;
    if (!resolve_path(request.target, web_root, &resolved_path, error)) {
        *response = make_text_response(400, "Bad Request", "Bad Request\n");
        return true;
    }

    std::string body;
    std::string cache_error;
    if (!file_cache::read_file(resolved_path, &body, &cache_error)) {
        *response = make_not_found_response(web_root);
        return true;
    }

    response->status = 200;
    response->reason = "OK";
    response->body = body;
    response->headers["Content-Type"] = mime_type_for_path(resolved_path);
    return true;
}
