# Learning Notes

## Phase 0 - Project Scaffold

### What a Makefile does
A Makefile is a tiny build script. It defines targets (like `build` and `run`) and the commands needed to produce them. When you run `make`, it checks timestamps and only rebuilds what changed.

### Key targets in this project
- `build` compiles `.cpp` files into `.o` objects, then links them into a single binary.
- `run` depends on `build`, then executes the binary.
- `clean` removes build artifacts so you can rebuild from scratch.
- `test` is a placeholder for future automated tests.

### How compilation works
- **Compile step:** each `.cpp` file becomes a `.o` object file.
- **Link step:** all `.o` files are combined into the final executable.

### Why this layout
- `src/` keeps implementation files together.
- `include/` is where public headers will live once the project grows.
- `www/` will hold static assets served by the HTTP server.
- `docs/` captures learning notes so the README stays clean.

### Next step
Phase 1 will replace the placeholder `main.cpp` with a TCP server that listens on port 8080 and sends a simple response.

## Phase 1 - TCP Hello World Server

### What we built
- A single-threaded TCP server that listens on port 8080.
- Sequential accept loop that replies with a fixed HTTP response.
- A small `send_all` helper to handle partial writes and EINTR.

### Key APIs used
- `socket()` to create a TCP socket.
- `setsockopt()` with `SO_REUSEADDR` (and `SO_REUSEPORT` when available) for faster restarts.
- `bind()`, `listen()`, and `accept()` for the server lifecycle.
- `send()` and `close()` for the response and cleanup.

### Safety and robustness
- Error checks on every syscall with early close on failures.
- Accept loop continues after transient errors.
- No external dependencies added.

### How to test
1. `make run`
2. `curl -v http://localhost:8080/`
3. You should see a `Hello World` body with a `200 OK` response.

### Limitations (expected for Phase 1)
- No HTTP parsing yet; every request gets the same response.
- Single-threaded and one connection handled at a time.
- No keep-alive or request size limits yet.

## Phase 2 - HTTP Parsing and Response Builder

### What we built
- A request reader that stops at `\r\n\r\n` with a 32 KB max size.
- Request line parsing into method, target, and version.
- Header parsing into a lowercase-key map.
- Body reading based on `Content-Length` when present.
- A response builder that always sets `Content-Length` and `Connection: close`.

### Safety and robustness
- Reject malformed request lines or headers with `400 Bad Request`.
- Fail fast when the request exceeds the max size.
- No chunked encoding or keep-alive yet.

### How to test
1. `make run`
2. `curl -v http://localhost:8080/`
3. `curl -v -d "hi" http://localhost:8080/`

## Phase 3 - Static File Serving

### What we built
- A static file handler rooted at the `www/` directory.
- Default `index.html` resolution for `/` and paths ending in `/`.
- Simple path normalization that blocks `..` traversal.
- MIME type mapping based on file extension.
- A custom `404.html` page if a file is missing.

### Safety and robustness
- Rejects invalid paths instead of serving from outside the web root.
- Enforces the same request size limits from Phase 2.
- Returns `405 Method Not Allowed` for non-GET requests.

### How to test
1. `make run`
2. Open `http://localhost:8080/`
3. Open `http://localhost:8080/style.css`
4. Open a missing path to see the 404 page

## Phase 4 - Thread-Per-Connection

### What we built
- A dedicated `handle_client` function that owns request parsing and response sending.
- A detached thread per accepted client so multiple requests can run in parallel.
- A mutex-protected error logger to avoid interleaved output across threads.

### Safety and robustness
- Each handler always closes its client socket.
- Thread spawn failures close the client socket immediately.
- Behavior stays the same as Phase 3 (static files, Connection: close).

### How to test
1. `make run`
2. In two terminals, run `curl http://localhost:8080/` simultaneously.
3. Both requests should complete without waiting for each other.

## Phase 5 - Thread Pool

### What we built
- A bounded worker pool that owns a fixed set of `std::thread` workers.
- A queue of accepted client sockets protected by a mutex and condition variable.
- Graceful shutdown on `SIGINT` by closing the listening socket and joining workers.

### Safety and robustness
- Accepted sockets are queued instead of spawning unlimited detached threads.
- The pool stops cleanly and drains or closes queued work during shutdown.
- Existing request handling stays unchanged inside the worker function.

## Phase 6 - Request Logging

### What we built
- A thread-safe logger that writes structured request lines to `server.log`.
- Timestamp, method, path, status, worker thread id, and latency in each log line.
- An in-memory ring buffer that keeps the last 1000 log entries.

### Safety and robustness
- Logging is protected by a mutex so concurrent workers do not interleave output.
- The logger is initialized and shut down with server startup and exit.

## Phase 7 - REST API and Frontend

### What we built
- A route layer that serves `/api/notes` before falling back to static files.
- In-memory notes storage protected by a mutex.
- JSON helpers for note/list responses and a small JSON body parser for note creation.
- API endpoints for listing, creating, and deleting notes.
- A browser UI in `www/` that calls the API with `fetch()`.

### Safety and robustness
- API paths return JSON errors for bad input, missing notes, and unsupported methods.
- Static files still serve from `www/` for everything outside `/api/`.
- The frontend stays on the same origin, so there is no extra CORS setup.

## Phase 8 - Optimization and Stress Testing

### File cache with shared mutex and LRU eviction

#### What we built
- A thread-safe file cache in `file_cache.cpp` / `file_cache.h` that sits between the static file handler and disk I/O.
- Uses `std::shared_mutex` so concurrent readers don't block each other — only cache insertions and evictions take an exclusive lock.
- LRU eviction policy: a `std::list<std::string>` tracks access order; when the cache exceeds `max_entries` (default 64), the least-recently-used entry is evicted.
- **mtime-based invalidation**: every cache hit re-checks `std::filesystem::last_write_time`. If the file was modified on disk since it was cached, the entry is refreshed. This means you can edit `www/` files without restarting the server.
- The cache is wired into `static_files.cpp` — both `build_static_response` and `make_not_found_response` go through `file_cache::read_file()` instead of hitting disk directly.
- Configurable at runtime via `file_cache::set_enabled()` and `file_cache::set_capacity()`.

#### Why this matters
- Static file reads are one of the most common operations. Without a cache, every `GET /` or `GET /style.css` opens the file, reads it, and closes it — even if the file hasn't changed.
- On repeated requests to the same files (e.g., the browser loading index.html, style.css, and app.js on every page visit), the cache eliminates redundant disk I/O.
- The shared mutex means N workers can all serve the same cached file simultaneously without blocking.

### Optional keep-alive support

#### What we built
- The server now supports HTTP/1.1 keep-alive. If the client sends `Connection: keep-alive`, the worker keeps the socket open and reads the next request.
- A **request loop** in `handle_client_internal`: after sending a response, if keep-alive was negotiated, the worker loops back to `read_http_request()` on the same socket.
- **Socket timeout**: after the first keep-alive response, `SO_RCVTIMEO` is set to 5 seconds on the client socket. If the client is idle for more than 5 seconds, `recv()` returns `EAGAIN` and the connection is closed. This prevents idle clients from holding workers indefinitely.
- **Per-connection request limit**: a maximum of 100 requests per connection. After 100 keep-alive responses, the server sends `Connection: close` and the loop terminates. This prevents a single client from monopolizing a worker thread forever.
- The response builder adds `Keep-Alive: timeout=5, max=100` so the client knows the server's limits.
- **Parse errors always close the connection**: if a request is malformed, the connection state is unreliable, so we send 400 and close immediately — no keep-alive after errors.

#### How to test keep-alive
1. `curl -v -H "Connection: keep-alive" http://localhost:8080/ http://localhost:8080/style.css`
2. Both URLs should be fetched over a single TCP connection (look for `* Connection #0 to host localhost left intact` in curl output).
3. The response headers should include `Connection: keep-alive` and `Keep-Alive: timeout=5, max=100`.

### Benchmarking

#### Tool
- `benchmark.py` in the project root exercises multiple workloads.
- Run with: `python benchmark.py --requests 2000 --concurrency 16`

#### What to measure
| Test | What it exercises |
|------|-------------------|
| Static GET / | File cache hits (after first request) |
| Static GET /app.js | Cache across different files |
| Keep-alive pipeline | Connection reuse, 100 reqs on one socket |
| API GET /api/notes | JSON serialization, mutex on notes store |
| API POST + DELETE | JSON parsing, state mutation |

#### Expected results (before vs after)
- **Before (Phase 7)**: every static file read hits disk (`ifstream` open/read/close). Every request is one connection (no keep-alive). No caching.
- **After (Phase 8)**: first request to each file populates the cache; subsequent requests for the same file are served from memory. Keep-alive eliminates TCP handshake overhead for repeated requests from the same client.

#### Running the benchmark
```bash
# 1. Start the server
./http_server

# 2. In another terminal, run the benchmark
python benchmark.py --requests 2000 --concurrency 16

# 3. Ctrl+C the server and check server.log for per-request latency
```

### Limitations and trade-offs

#### File cache
- **Memory-only**: cached file contents live in RAM. Large files (>1 MB) served to many concurrent clients could consume significant memory. The default 64-entry cap limits worst-case usage, but a production system would add a byte-size limit.
- **No TTL beyond mtime**: files are cached indefinitely as long as they don't change on disk and fit within the LRU cap. A production cache might add a time-based TTL.
- **No cache warming**: the cache is cold at startup. The first request for each file still hits disk.
- **Cache-busting**: query strings (`/style.css?v=2`) cause cache misses because the path doesn't match. This is actually correct — the query string is stripped before path resolution, so `/style.css?v=2` resolves to the same file as `/style.css`, but the cache key is the resolved filesystem path, so it hits the cache.

#### Keep-alive
- **One request at a time per connection**: the server reads one request, processes it, sends the response, then reads the next. It does NOT support HTTP pipelining (sending multiple requests before reading responses). This is a simplification — true pipelining is complex and rarely used in practice.
- **No idle timeout before first keep-alive response**: the timeout is only set after the first keep-alive response is sent. An initial slow client could hold a worker during the first request. A production server would set `SO_RCVTIMEO` immediately after accept.
- **Worker-per-connection**: during keep-alive, a worker thread is dedicated to that connection. If many clients keep connections alive, the thread pool can be exhausted. The 5-second timeout and 100-request limit mitigate this, but a production system would use non-blocking I/O (epoll/kqueue/IOCP).
- **No `Connection: close` override from handlers**: the server always honors the client's keep-alive request for valid responses. A handler can't force-close the connection (e.g., after a 500 error). This is acceptable for a learning project.

#### General limitations (carried from earlier phases)
- No TLS/HTTPS support (would need OpenSSL or similar).
- No chunked transfer encoding (request or response).
- No HTTP/2 or HTTP/3.
- No request pipelining.
- No file descriptor limits or overload protection beyond the thread pool size.
- In-memory notes store is lost on server restart.

### Lessons learned

- **`std::shared_mutex` is perfect for read-heavy caches**: multiple readers hold shared locks concurrently; only evictions and insertions need exclusive access. The pattern of `shared_lock` for reads and `unique_lock` for writes is clean and performant.
- **`std::filesystem::last_write_time` is inexpensive enough for per-request checks**: it's a stat() syscall under the hood, which is fast on modern filesystems with metadata caching.
- **`SO_RCVTIMEO` is a simple way to prevent hung connections**: without it, a keep-alive client that goes silent would hold a worker thread forever. The timeout turns an idle connection into a clean close.
- **Keep-alive + thread-per-connection doesn't scale to C10K**: each kept-alive connection ties up a thread. For high-concurrency keep-alive, you need event-driven I/O. But for a learning project handling dozens of concurrent users, it works well.
- **Incremental phases work**: each phase added one well-scoped capability without breaking previous phases. The Phase 8 cache slots into the existing static file handler with a one-line change.
