# Multi-Threaded HTTP Server (C++17)

From-scratch C++17 HTTP/1.1 server using POSIX sockets, built in phases from TCP hello to parsing, static files, threading, logging, REST, caching, keep-alive, and benchmarks. Zero deps, make build, learning notes.

## Build

```
make build
```

## Run

```
make run
```

## Clean

```
make clean
```

## Benchmark

```
python benchmark.py --requests 500 --concurrency 8
```

Full results and analysis in [`BENCHMARK.md`](BENCHMARK.md).

### Quick results (Phase 8, 8 workers)

| Test | Req/sec | p50 | p90 |
|------|---------|-----|-----|
| Static GET / | 584.5 | 12.58ms | 18.43ms |
| Static GET /app.js | 567.0 | 12.67ms | 20.61ms |
| Static GET /style.css | 520.7 | 13.01ms | 22.06ms |
| Keep-Alive (100 reqs) | ✅ | 1.45ms | 1.55ms |
| API GET /api/notes | 357.6 | 12.06ms | 16.83ms |

## Project Structure

- `src/` - C++ source files
- `include/` - Header files
- `www/` - Web root for static files
- `docs/` - Learning notes and phase write-ups
- `benchmark.py` - Phase 8 load testing tool
- `BENCHMARK.md` - Detailed benchmark results and analysis
- `server.log` - Structured request log (generated at runtime)

## Roadmap (Phases)

| Phase | Feature | Status |
|-------|---------|--------|
| 0 | Project scaffold and build workflow | ✅ Done |
| 1 | TCP Hello World server | ✅ Done |
| 2 | Parse HTTP requests | ✅ Done |
| 3 | Serve static files | ✅ Done |
| 4 | Thread-per-connection | ✅ Done |
| 5 | Thread pool with graceful shutdown | ✅ Done |
| 6 | Request logging (file + ring buffer) | ✅ Done |
| 7 | REST API + browser frontend | ✅ Done |
| 8 | File cache (LRU + shared_mutex) + keep-alive + benchmarking | ✅ Done |

## Features

- **HTTP/1.1** request parsing with header normalization
- **Static file serving** with MIME type detection and path traversal protection
- **Thread pool** with configurable worker count, graceful SIGINT shutdown
- **Structured logging** to file + in-memory ring buffer (last 1000 entries)
- **REST API** for notes CRUD with JSON responses
- **Browser frontend** (vanilla JS, no framework)
- **File cache** with LRU eviction, mtime-based invalidation, shared_mutex for concurrent reads
- **Keep-alive** support with socket timeout and per-connection request limit
