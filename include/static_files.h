#ifndef STATIC_FILES_H
#define STATIC_FILES_H

#include <string>

#include "http.h"

bool build_static_response(const HttpRequest& request, const std::string& web_root,
                           HttpResponse* response, std::string* error);

#endif
