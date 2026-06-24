#include "json_util.h"

#include <sstream>

std::string json_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::string build_note_json(const Note& note) {
    std::ostringstream out;
    out << "{\"id\":" << note.id << ",\"text\":\"" << json_escape(note.text) << "\"}";
    return out.str();
}

std::string build_notes_json(const std::vector<Note>& notes) {
    std::ostringstream out;
    out << "{\"notes\":[";
    for (size_t i = 0; i < notes.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << build_note_json(notes[i]);
    }
    out << "]}";
    return out.str();
}
