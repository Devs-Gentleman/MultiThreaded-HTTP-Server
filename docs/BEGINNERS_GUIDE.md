# Beginner's Guide: Multi-Threaded HTTP Server

> **Who this is for:** Someone who just got handed this project and wants to understand what it does, how it works, and how data flows through it — starting from zero.

---

## 1. The 30-Second Big Picture

This program is a **web server** — the same kind of software that powers every website you visit. You run it on your computer, and then you (or anyone on your network) can open a browser to `http://localhost:8080` and see a web page it serves. It can also store and manage "notes" via a simple API (like a tiny version of what Google Keep or Apple Notes does behind the scenes).

| Concept | What it means in this project |
|---------|-------------------------------|
| **HTTP Server** | A program that listens for browser requests and sends back responses |
| **Multi-threaded** | It can handle multiple users at the same time using parallel "workers" |
| **Static files** | HTML/CSS/JS files on disk that get sent to the browser as-is |
| **REST API** | URLs like `/api/notes` that let code (or the browser) create/read/delete data |
| **Thread pool** | A fixed team of worker threads that share the workload instead of spawning unlimited threads |
| **Keep-alive** | Reusing the same connection for multiple requests instead of hanging up after each one |
| **File cache** | Keeping recently-read files in RAM so the server doesn't hit the disk every time |

---

## 2. What Files Are in This Project?

```
MultiThreaded HTTP Server/
├── src/                    ← C++ source code (the logic)
│   ├── main.cpp            ← Entry point: starts server, routes requests, glues everything together
│   ├── http.cpp            ← HTTP parser & response builder
│   ├── thread_pool.cpp     ← Worker thread pool (manages parallel workers)
│   ├── static_files.cpp    ← Serves HTML/CSS/JS files from disk
│   ├── file_cache.cpp      ← LRU cache for file contents (avoids repeated disk reads)
│   ├── notes_store.cpp     ← In-memory notes database (create, list, delete)
│   ├── json_util.cpp       ← Tiny JSON serialization helpers
│   └── logger.cpp          ← Writes structured request logs to a file
├── include/                ← Header files (.h) — declare what each .cpp file exports
├── www/                    ← The "web root" — files the browser can access
│   ├── index.html          ← Main page: a notes app UI
│   ├── app.js              ← Browser-side JavaScript: calls the API, renders notes
│   ├── style.css           ← Styles the page
│   └── 404.html            ← Shown when you visit a URL that doesn't exist
├── docs/
│   ├── LEARNING.md         ← Phase-by-phase learning notes
│   └── BEGINNERS_GUIDE.md  ← YOU ARE HERE
├── benchmark.py            ← Python script that stress-tests the server
├── BENCHMARK.md            ← Results from running the benchmark
├── Makefile                ← Build instructions (tells the compiler how to assemble the program)
└── server.log              ← Log file created when the server runs (one line per request)
```

---

## 3. The Complete Data Flow: What Happens When You Type `http://localhost:8080` in Your Browser?

This is the most important section. We'll trace every single step from the moment your browser hits Enter to the moment the page appears on screen.

### 3.1 The Diagram

```
 BROWSER                    YOUR PROGRAM (http_server)                    DISK / MEMORY
 ───────                    ───────────────────────────                    ─────────────

   ①  ───── TCP SYN ─────→
                             ② Linux kernel accepts connection
                             ③ main() loop accepts client_fd
                             ④ Thread pool picks it up

   ⑤  ──── "GET / HTTP/1.1\r\n..." ───→
                             ⑥ read_http_request() parses raw bytes
                                ┌─────────────────────────┐
                                │ method:  "GET"          │
                                │ target:  "/"            │
                                │ version: "HTTP/1.1"     │
                                │ headers: {...}          │
                                │ body:    ""             │
                                │ keep_alive: false       │
                                └─────────────────────────┘

                             ⑦ Route check: starts with "/api/"?
                                → No, fall through to static files

                             ⑧ resolve_path("/", "www")
                                → "www/index.html"

                             ⑨ file_cache::read_file("www/index.html")
                                → Is it in the cache? (check LRU + mtime)
                                → First time: read from disk, store in cache
                                → Subsequent: read from memory (shared_mutex)

                             ⑩ build_http_response()
                                ┌─────────────────────────────────┐
                                │ HTTP/1.1 200 OK                 │
                                │ Content-Type: text/html         │
                                │ Content-Length: 1342            │
                                │ Connection: close               │
                                │                                 │
                                │ <html>...page content...</html> │
                                └─────────────────────────────────┘

   ⑪  ←─── HTTP response bytes ────
                             ⑫ logger writes to server.log
                             ⑬ close(client_fd) — hang up
   ───────                    ───────────────────────────
  Browser renders index.html
  Browser parses HTML, finds
  <link rel="stylesheet" href="/style.css">
  <script src="/app.js"></script>

  Browser makes TWO MORE requests:
   GET /style.css  → (same flow: ⑤→⑬)
   GET /app.js     → (same flow: ⑤→⑬, both served from cache!)

   Browser runs app.js
   app.js calls fetch('/api/notes')
                             ⑭ THIS TIME: route check hits!
                                build_api_response()
                                → handle_notes_list()
                                → g_notes_store.list_notes()
                                → build_notes_json()
                                → {"notes":[...]}
                             ⑮ Browser receives JSON, renders notes on the page
```

### 3.2 Step-by-Step Narration

Now let's walk through each numbered step in plain English.

---

#### ① The Browser Connects (TCP Handshake)

When you type `http://localhost:8080` and hit Enter, your browser does a **TCP handshake** with the server. TCP is the reliable-connection protocol that sits underneath HTTP. This is handled entirely by the operating system — your program doesn't see the handshake bytes. The OS just notifies your program that "someone connected."

---

#### ②-③ The OS Accepts the Connection

Inside `main()` (lines 433-447 of `src/main.cpp`), there's an infinite loop:

```cpp
while (!stop_requested) {
    int client_fd = ::accept(server_fd, ...);   // ← WAITS here until a browser connects
    pool.enqueue(client_fd);                     // ← hands the connection to a worker thread
}
```

- `accept()` is a blocking system call. The program **sleeps** at this line until a browser connects.
- When a browser connects, the OS returns a new number called a **file descriptor** (`client_fd`). Think of this as a "ticket number" that uniquely identifies this browser's connection.
- That ticket gets pushed into the thread pool's job queue.

Key code: `src/main.cpp:433-447`

---

#### ④ The Thread Pool Picks Up the Job

The thread pool (in `src/thread_pool.cpp`) pre-creates a fixed number of worker threads (usually 8, based on your CPU cores). Each worker runs `worker_loop()`:

```cpp
void ThreadPool::worker_loop() {
    while (true) {
        int fd = -1;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            // ↑ sleep until a job arrives, or we're shutting down
            if (stopping_ && jobs_.empty()) return;
            fd = jobs_.front(); jobs_.pop();
        }
        if (fd >= 0) handle_client(fd);   // ← process the connection
    }
}
```

Key concepts:
- **Mutex (`mu_`)**: A lock that ensures only one worker reads from the queue at a time. Prevents two workers from grabbing the same `client_fd`.
- **Condition variable (`cv_`)**: A signal mechanism. Workers sleep on `cv_.wait()` (using zero CPU) until the main thread calls `cv_.notify_one()`. This is like a waiter sleeping until the kitchen bell rings.
- **`stopping_`**: An atomic flag that tells workers to shut down (set when you press Ctrl+C).

Key code: `src/thread_pool.cpp:56-78`

---

#### ⑤ The Browser Sends the HTTP Request

Once the TCP connection is established, the browser sends raw bytes that look like this:

```
GET / HTTP/1.1\r\n
Host: localhost:8080\r\n
User-Agent: Mozilla/5.0 ...\r\n
Accept: text/html,...\r\n
Connection: keep-alive\r\n
\r\n
```

That `\r\n\r\n` at the end is the **header terminator** — it marks "I'm done sending headers." For GET requests, there's no body, so this is the entire request.

---

#### ⑥ The HTTP Parser (`read_http_request`) Decodes the Bytes

Inside `src/http.cpp`, the function `read_http_request()`:

1. **Reads bytes in chunks** (4KB at a time) using `recv()`, appending to a buffer until it finds `\r\n\r\n` (the double-newline that marks end of headers).
2. **Splits** the first line into three pieces: `GET`, `/`, and `HTTP/1.1`.
3. **Parses headers** into a lowercase-key map. Header names are case-insensitive per the HTTP spec, so the parser lowercases everything (`Connection` → `connection`).
4. **Detects keep-alive**: if the `Connection` header contains `keep-alive`, sets `request.keep_alive = true`.
5. **Reads the body** (if any) based on the `Content-Length` header.

The result is a neat `HttpRequest` struct:

```cpp
struct HttpRequest {
    std::string method;       // "GET"
    std::string target;       // "/"
    std::string version;      // "HTTP/1.1"
    std::unordered_map<std::string, std::string> headers;
    std::string body;         // "" (empty for GET)
    bool keep_alive;          // true or false
};
```

Key code: `src/http.cpp:131-224`

If parsing fails (malformed request, too large, client disconnects), the function returns `false` and the worker sends back `400 Bad Request` and closes the connection.

---

#### ⑦ Route Check: API or Static?

Back in `handle_client_internal()` (`src/main.cpp:294-363`), the server decides what to do with the parsed request:

```cpp
if (!build_api_response(request, &response, &handler_error)) {
    // Not an API route → try static files
    if (!build_static_response(request, kWebRoot, &response, &handler_error)) {
        // Neither worked → 500 Internal Server Error
        response = make_text_response(500, ...);
    }
}
```

The routing logic is simple:
- If the URL starts with `/api/`, it's an API call. The server checks the exact path and HTTP method against a table:

| Method  | Path              | Handler               |
|---------|-------------------|-----------------------|
| GET     | `/api/notes`      | List all notes        |
| POST    | `/api/notes`      | Create a new note     |
| DELETE  | `/api/notes/{id}` | Delete a specific note|

- If the URL does **not** start with `/api/`, it's a static file request.

Key code: `src/main.cpp:258-292`, `src/main.cpp:44-48`

---

#### ⑧ Path Resolution (`resolve_path`)

For static file requests, `build_static_response()` calls `resolve_path()` to translate the URL into a filesystem path. This is a critical security step:

```
URL:  "/"              →  "www/index.html"      (default page)
URL:  "/style.css"     →  "www/style.css"
URL:  "/../secret"     →  REJECTED (path traversal blocked!)
URL:  "/images/photo"  →  "www/images/photo"     (if it existed)
```

The resolver:
1. Strips query strings (`?foo=bar`) and fragments (`#section`).
2. Splits the path into segments by `/`.
3. **Blocks** `..` segments (this prevents attackers from reading files outside `www/`).
4. Appends `index.html` if the path ends in `/` (directory → default page).
5. Prepends `www/` to get the final filesystem path.

Key code: `src/static_files.cpp:26-75`

---

#### ⑨ File Cache (`file_cache::read_file`)

Now the server needs the actual file contents. Instead of always reading from disk, it checks the cache first:

```
read_file("www/index.html")
    │
    ├─ Cache HIT (file in cache, mtime matches)?
    │   → Copy from memory (shared_lock — multiple readers allowed!)
    │   → Move to front of LRU list (mark as "recently used")
    │   → Return immediately ✓
    │
    └─ Cache MISS (not in cache, or file modified on disk)?
        → Read from disk with ifstream
        → Insert into cache (unique_lock — exclusive write access)
        → Evict oldest entry if cache is full (LRU eviction)
        → Return ✓
```

The cache uses:
- **`std::shared_mutex`**: Multiple threads can READ from the cache simultaneously (shared lock). Only when adding/removing entries does a thread need exclusive (unique) access. This is the key to good performance — 8 workers can all serve `index.html` without blocking each other.
- **LRU list**: A `std::list` tracking access order. When the cache is full, the **least recently used** entry gets evicted.
- **mtime check**: Even on a cache hit, the code checks `last_write_time()` on the filesystem. If you edited the file, the cache automatically refreshes it. No need to restart the server.

Key code: `src/file_cache.cpp:102-158`

---

#### ⑩ Building the HTTP Response

Once the body is ready (from cache or disk), `build_http_response()` assembles the complete response string:

```
HTTP/1.1 200 OK\r\n
Content-Type: text/html\r\n
Content-Length: 1342\r\n
Connection: close\r\n
\r\n
<!DOCTYPE html>
<html>
...the actual page...
</html>
```

Important details:
- **`Content-Length`** is always set (tells the browser exactly how many bytes to read).
- **`Connection`** is `close` by default, or `keep-alive` if the client requested it.
- **`Content-Type`** is set based on file extension (`.html` → `text/html`, `.css` → `text/css`, `.js` → `application/javascript`).

Key code: `src/http.cpp:226-267`, `src/static_files.cpp:86-108`

---

#### ⑪-⑬ Sending and Logging

Back in `handle_client_internal()`:

```cpp
std::string payload = build_http_response(response, response_keep_alive);
send_all(client_fd, payload.data(), payload.size());   // ← send bytes
// ... measure latency ...
server_logger::log_request(method, target, status, thread_id, latency_ms);
```

`send_all()` is a helper that handles the messy reality of TCP: `send()` might only send PART of your data if the OS buffer is full, or it might be interrupted by a signal (`EINTR`). `send_all()` keeps retrying until all bytes are sent or the connection breaks.

The logger writes a structured line to `server.log`:
```
2026-06-24T12:00:00.123 level=info method=GET path=/ status=200 thread=1 latency_ms=12
```
And also stores it in a ring buffer (last 1000 entries) in memory.

Key code: `src/main.cpp:50-67` (send_all), `src/main.cpp:335-342` (logging), `src/logger.cpp:65-86`

---

#### ⑫ Keep-Alive: The Loop

If the client requested keep-alive AND the per-connection request limit (100) hasn't been reached, the worker **doesn't close the connection**. Instead, it:

1. Sets a **5-second read timeout** (`SO_RCVTIMEO`) on the socket.
2. **Loops back** to `read_http_request()` on the same socket.
3. Reads the next request, processes it, sends response, loops again...

This means one TCP connection can handle up to 100 requests before hanging up, saving the overhead of repeated TCP handshakes. If the client goes silent for 5 seconds, the `recv()` call times out and the connection closes gracefully.

Key code: `src/main.cpp:303-360`

---

#### ⑬ The API Flow (When You Use the Notes App)

The browser frontend (`www/app.js`) calls the API using `fetch()`:

```javascript
// In app.js — runs in the browser, not the server
fetch('/api/notes')                          // list all
fetch('/api/notes', { method: 'POST', ... }) // create new
fetch('/api/notes/3', { method: 'DELETE' })  // delete #3
```

On the server side, these hit `build_api_response()`:

**Listing notes:**
```
GET /api/notes
  → handle_notes_list()
    → g_notes_store.list_notes()     // lock mutex, copy vector
    → build_notes_json(notes)        // serialize to {"notes":[...]}
    → make_json_response(200, ...)
```

**Creating a note:**
```
POST /api/notes  (body: {"text":"Buy milk"})
  → parse_json_string_field(body, "text", &text)
    → finds "text": in the JSON, extracts string value
    → handles escape sequences (\n, \t, \\, \")
  → g_notes_store.create_note(text)
    → lock mutex
    → assign next_id_ (auto-incrementing)
    → push to vector
  → build_note_json(note)
  → make_json_response(201, "Created", ...)
  → sets Location header: /api/notes/1
```

**Deleting a note:**
```
DELETE /api/notes/1
  → parse_note_id_from_target("/api/notes/1", &id)
    → extracts "1" from the URL
  → g_notes_store.delete_note(1)
    → lock mutex, search vector, erase if found
  → make_json_response(204, "No Content", ...)
    // 204 = success, no body (standard for DELETE)
```

Key code: `src/main.cpp:194-256`, `src/notes_store.cpp:5-28`, `src/json_util.cpp:23-39`

---

## 4. Component Reference Card

Here's a quick reference for each source file — what it does, what it depends on, and who depends on it.

### `src/main.cpp` — The Conductor

| | |
|---|---|
| **Role** | Entry point. Starts the server, runs the accept loop, routes requests, handles signals. |
| **Key functions** | `main()`, `handle_client_internal()`, `build_api_response()`, `send_all()` |
| **Depends on** | Everything (http, thread_pool, static_files, notes_store, json_util, logger) |
| **Called by** | The OS (it's `main()`) |

### `src/http.cpp` — The Translator

| | |
|---|---|
| **Role** | Parses raw bytes into `HttpRequest` structs. Builds `HttpResponse` structs into raw bytes. |
| **Key functions** | `read_http_request()`, `build_http_response()`, `make_text_response()` |
| **Depends on** | Nothing internal (standalone) |
| **Used by** | `main.cpp` (`handle_client_internal`) |

### `src/thread_pool.cpp` — The Team Manager

| | |
|---|---|
| **Role** | Creates N worker threads. Accept loop pushes client_fds into a queue. Workers pop and process. |
| **Key functions** | `ThreadPool()`, `enqueue()`, `stop()`, `worker_loop()` |
| **Data structures** | `std::queue<int>` (job queue), `std::mutex` + `std::condition_variable`, `std::atomic<bool>` (stop flag) |
| **Depends on** | Nothing internal except `extern void handle_client(int)` declared in main.cpp |
| **Used by** | `main.cpp` |

### `src/static_files.cpp` — The Librarian

| | |
|---|---|
| **Role** | Translates URLs to filesystem paths (safely). Reads files via cache. Detects MIME types. |
| **Key functions** | `build_static_response()`, `resolve_path()`, `make_not_found_response()` |
| **Depends on** | `file_cache` (for file reads), `http` (for response structs) |
| **Used by** | `main.cpp` |

### `src/file_cache.cpp` — The Short-Term Memory

| | |
|---|---|
| **Role** | LRU cache for file contents. Avoids repeated disk I/O. Thread-safe with shared_mutex. |
| **Key functions** | `file_cache::read_file()`, `set_enabled()`, `set_capacity()` |
| **Data structures** | `std::unordered_map<string, Entry>` (cache), `std::list<string>` (LRU order), `std::shared_mutex` |
| **Depends on** | `std::filesystem` (for `last_write_time`) |
| **Used by** | `static_files.cpp` |

### `src/notes_store.cpp` — The Database

| | |
|---|---|
| **Role** | In-memory CRUD store for notes. Thread-safe. Data is lost on server restart. |
| **Key functions** | `create_note()`, `list_notes()`, `delete_note()` |
| **Data structures** | `std::vector<Note>`, `std::mutex`, `int next_id_` |
| **Depends on** | Nothing internal |
| **Used by** | `main.cpp` (via global `g_notes_store`) |

### `src/json_util.cpp` — The Serializer

| | |
|---|---|
| **Role** | Escapes strings for JSON. Builds JSON objects from Note structs. |
| **Key functions** | `json_escape()`, `build_note_json()`, `build_notes_json()` |
| **Depends on** | `notes_store.h` (for the `Note` struct definition) |
| **Used by** | `main.cpp` |

### `src/logger.cpp` — The Historian

| | |
|---|---|
| **Role** | Writes structured request logs to `server.log` and keeps a ring buffer in memory. |
| **Key functions** | `init_logger()`, `log_request()`, `shutdown_logger()` |
| **Data structures** | `std::ofstream` (file), `std::vector<Entry>` (ring buffer, max 1000) |
| **Depends on** | Nothing internal |
| **Used by** | `main.cpp` |

---

## 5. Concurrency Model: How Threads Work

This is the hardest concept for beginners, so let's visualize it.

```
         ┌───────────┐
         │  main()   │  ← runs on the "main thread"
         │           │
         │ accept()  │  ← waits for browser connections
         │   ↓       │
         │ enqueue() │  ← pushes client_fd into queue
         └─────┬─────┘
               │
         ┌─────▼─────┐
         │  JOB QUEUE │  ← shared, protected by mutex
         │  [fd:5]    │
         │  [fd:6]    │
         │  [fd:7]    │
         └─────┬─────┘
               │  (cv_.notify_one() wakes one sleeping worker)
               │
    ┌──────────┼──────────┬──────────┐
    ▼          ▼          ▼          ▼
┌───────┐  ┌───────┐  ┌───────┐  ┌───────┐
│Worker1│  │Worker2│  │Worker3│  │  ...  │
│       │  │       │  │       │  │       │
│ pop() │  │ pop() │  │ pop() │  │       │
│  ↓    │  │  ↓    │  │  ↓    │  │       │
│handle │  │handle │  │handle │  │       │
│client │  │client │  │client │  │       │
│ (fd)  │  │ (fd)  │  │ (fd)  │  │       │
│  ↓    │  │  ↓    │  │  ↓    │  │       │
│close  │  │close  │  │close  │  │       │
└───────┘  └───────┘  └───────┘  └───────┘
   ↑          ↑          ↑          ↑
   └──────────┴──────────┴──────────┘
          All workers run simultaneously

   Each worker: wait → get job → parse → route → respond → log → close → repeat
```

**Key insight:** Each worker handles ONE request at a time. With 8 workers, up to 8 browsers can be served simultaneously. The 9th browser waits in the queue until a worker finishes.

**Mutexes protect shared state:**
- `mu_` in ThreadPool: protects the job queue
- `server_logger::mu`: protects the log file and ring buffer
- `NotesStore::mutex_`: protects the notes list
- `file_cache::Cache::mutex` (shared_mutex): protects the cache (shared for reads, exclusive for writes)

**The rule of thumb:** If two threads could touch the same data at the same time, a mutex must protect it. Every mutex in this project follows that rule.

---

## 6. How to Build and Run

### Prerequisites
- A C++17 compiler (g++ or clang++)
- Python 3 (for the benchmark script)
- `make` (for the build system)

### Build
```bash
cd "D:\MultiThreaded HTTP Server"
make build
```

This compiles every `.cpp` file into a `.o` object file, then links them into a single binary called `http_server`.

### Run
```bash
make run
# or: ./http_server
```

You should see:
```
Listening on port 8080...
```

### Test in a Browser
Open `http://localhost:8080/` — you should see a notes app where you can create and delete notes.

### Test with curl
```bash
# Get the homepage
curl -v http://localhost:8080/

# List notes
curl http://localhost:8080/api/notes

# Create a note
curl -d '{"text":"hello world"}' http://localhost:8080/api/notes

# Delete note #1
curl -X DELETE http://localhost:8080/api/notes/1

# Test keep-alive (multiple requests on one connection)
curl -v -H "Connection: keep-alive" \
  http://localhost:8080/ \
  http://localhost:8080/style.css
```

### Run the Benchmark
```bash
# Terminal 1: start the server
./http_server

# Terminal 2: run the benchmark
python benchmark.py --requests 500 --concurrency 8
```

### Stop the Server
Press `Ctrl+C` — the server shuts down gracefully (closes the listening socket, drains remaining jobs, joins all worker threads).

---

## 7. Frequently Asked Questions

### Why does the server use `send_all()` instead of just `send()`?

The POSIX `send()` function can do partial writes. If you tell it to send 1000 bytes, it might send only 600 and return 600, expecting you to call again for the rest. It can also return -1 with `EINTR` (interrupted by a signal). `send_all()` handles both cases in a loop.

### What happens if someone sends a request larger than 32KB?

The parser (`read_until_marker`) tracks the total bytes received. If it exceeds `kMaxRequestBytes` (32KB), it returns an error and the server sends `400 Bad Request`. This prevents a malicious client from exhausting server memory by sending an infinitely large request.

### Why does the server block `..` in paths?

Without path traversal protection, a request like `GET /../../../etc/passwd` could read arbitrary files on your computer outside the `www/` directory. `resolve_path()` rejects any segment containing `..`, keeping all file access confined to `www/`.

### What's the difference between `shared_lock` and `unique_lock`?

- `shared_lock` (on a `shared_mutex`): Multiple threads can hold this simultaneously. Use for **reading**.
- `unique_lock` (on a `shared_mutex`): Only ONE thread can hold this. All other readers AND writers are blocked. Use for **writing**.

The file cache uses shared_lock for cache hits (N concurrent readers all get the file at once) and unique_lock for cache misses (only one thread inserts/evicts at a time).

### Why are the notes lost when I restart the server?

The notes store is in-memory (`std::vector<Note>`). There's no database or file persistence. This is intentional for a learning project — adding a real database would be a natural Phase 9.

### What's the biggest limitation of this server?

The thread-per-connection model. Each kept-alive connection ties up a worker thread for up to 5 seconds (the socket timeout). With 8 workers, you can only serve 8 long-lived connections simultaneously. A production server would use **non-blocking I/O** (epoll on Linux, kqueue on macOS, IOCP on Windows) where one thread can monitor thousands of connections. But that's significantly more complex — hence this project's incremental, learnable approach.

---

## 8. Where to Go Next

If you want to understand this project deeply:

1. **Read the code in order** of the phases: `main.cpp` → `http.cpp` → `static_files.cpp` → `thread_pool.cpp` → `logger.cpp` → `notes_store.cpp` → `json_util.cpp` → `file_cache.cpp`
2. **Run the server** and make requests with `curl -v` — the `-v` flag shows you the raw HTTP headers being sent and received.
3. **Read `server.log`** after making some requests — each line tells you what happened.
4. **Add a `cout` or breakpoint** inside `handle_client_internal()` to watch each request flow through.
5. **Try breaking things**: what happens if you send a malformed request with netcat? What if you request a path with `..` in it?
6. **Read the phase-by-phase learning notes** in `docs/LEARNING.md` for the "why" behind each design decision.
