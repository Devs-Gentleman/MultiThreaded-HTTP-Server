#include "notes_store.h"

#include <mutex>

Note NotesStore::create_note(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    Note note;
    note.id = next_id_++;
    note.text = text;
    notes_.push_back(note);
    return note;
}

bool NotesStore::delete_note(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = notes_.begin(); it != notes_.end(); ++it) {
        if (it->id == id) {
            notes_.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<Note> NotesStore::list_notes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return notes_;
}
