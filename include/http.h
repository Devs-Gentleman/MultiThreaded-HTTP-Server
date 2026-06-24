#ifndef HTTP_H
#define HTTP_H

#include <cstddef>
#include <string>
#include <unordered_map>

struct HttpRequest {
    std::string method;
    std::string target;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    bool keep_alive = false;
};

struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

bool read_http_request(int fd, size_t max_bytes, HttpRequest* out, std::string* error);
std::string build_http_response(const HttpResponse& response, bool keep_alive = false);
HttpResponse make_text_response(int status, const std::string& reason, const std::string& body);

#endif
