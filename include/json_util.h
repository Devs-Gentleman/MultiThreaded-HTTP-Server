#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include <string>
#include <vector>

#include "notes_store.h"

std::string json_escape(const std::string& value);
std::string build_note_json(const Note& note);
std::string build_notes_json(const std::vector<Note>& notes);

#endif
