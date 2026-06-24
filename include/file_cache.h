#ifndef FILE_CACHE_H
#define FILE_CACHE_H

#include <cstddef>
#include <string>

namespace file_cache {

void set_enabled(bool enabled);
bool is_enabled();
void set_capacity(std::size_t capacity);
std::size_t capacity();

bool read_file(const std::string& path, std::string* body, std::string* error);

}  // namespace file_cache

#endif
