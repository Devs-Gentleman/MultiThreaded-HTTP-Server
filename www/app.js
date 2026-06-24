const state = {
  notes: [],
};

const notesList = document.getElementById('notes-list');
const noteInput = document.getElementById('note-input');
const addButton = document.getElementById('add-note');
const refreshButton = document.getElementById('refresh-notes');
const statusLine = document.getElementById('status');

function setStatus(message, isError = false) {
  statusLine.textContent = message;
  statusLine.style.color = isError ? '#8d2b2b' : '#6f5844';
}

function renderNotes() {
  notesList.innerHTML = '';

  if (state.notes.length === 0) {
    const empty = document.createElement('li');
    empty.className = 'empty';
    empty.textContent = 'No notes yet. Add one above.';
    notesList.appendChild(empty);
    return;
  }

  for (const note of state.notes) {
    const item = document.createElement('li');
    item.className = 'note-item';

    const textWrap = document.createElement('div');
    const text = document.createElement('div');
    text.className = 'note-text';
    text.textContent = note.text;
    textWrap.appendChild(text);

    const meta = document.createElement('span');
    meta.className = 'note-meta';
    meta.textContent = `Note #${note.id}`;
    textWrap.appendChild(meta);

    const del = document.createElement('button');
    del.type = 'button';
    del.className = 'delete-note';
    del.textContent = 'Delete';
    del.addEventListener('click', () => deleteNote(note.id));

    item.appendChild(textWrap);
    item.appendChild(del);
    notesList.appendChild(item);
  }
}

async function fetchNotes() {
  setStatus('Loading notes...');
  try {
    const response = await fetch('/api/notes');
    const payload = await response.json();
    state.notes = Array.isArray(payload.notes) ? payload.notes : [];
    renderNotes();
    setStatus(`Loaded ${state.notes.length} note${state.notes.length === 1 ? '' : 's'}.`);
  } catch (error) {
    setStatus(`Failed to load notes: ${error.message}`, true);
  }
}

async function addNote() {
  const text = noteInput.value.trim();
  if (!text) {
    setStatus('Type a note first.', true);
    noteInput.focus();
    return;
  }

  addButton.disabled = true;
  setStatus('Saving note...');

  try {
    const response = await fetch('/api/notes', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({ text }),
    });

    if (!response.ok) {
      const payload = await response.json().catch(() => ({}));
      throw new Error(payload.error || `Request failed with ${response.status}`);
    }

    noteInput.value = '';
    await fetchNotes();
    noteInput.focus();
  } catch (error) {
    setStatus(`Failed to save note: ${error.message}`, true);
  } finally {
    addButton.disabled = false;
  }
}

async function deleteNote(id) {
  setStatus(`Deleting note #${id}...`);

  try {
    const response = await fetch(`/api/notes/${id}`, {
      method: 'DELETE',
    });

    if (!response.ok && response.status !== 204) {
      const payload = await response.json().catch(() => ({}));
      throw new Error(payload.error || `Request failed with ${response.status}`);
    }

    await fetchNotes();
  } catch (error) {
    setStatus(`Failed to delete note: ${error.message}`, true);
  }
}

document.addEventListener('DOMContentLoaded', () => {
  addButton.addEventListener('click', addNote);
  refreshButton.addEventListener('click', fetchNotes);
  noteInput.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') {
      event.preventDefault();
      addNote();
    }
  });

  fetchNotes();
});
