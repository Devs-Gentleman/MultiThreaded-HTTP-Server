#ifndef NOTES_STORE_H
#define NOTES_STORE_H

#include <string>
#include <vector>
#include <mutex>

struct Note {
    int id = 0;
    std::string text;
};

class NotesStore {
public:
    Note create_note(const std::string& text);
    bool delete_note(int id);
    std::vector<Note> list_notes() const;

private:
    mutable std::mutex mutex_;
    std::vector<Note> notes_;
    int next_id_ = 1;
};

#endif
