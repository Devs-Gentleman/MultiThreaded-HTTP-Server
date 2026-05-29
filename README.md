<<<<<<< HEAD
# MultiThreaded-HTTP-Server
From-scratch C++17 HTTP/1.1 server using POSIX sockets, built in phases from TCP hello to parsing, static files, threading, logging, REST, caching, and benchmarks. Zero deps, make build, learning notes.
=======
# Multi-Threaded HTTP Server (C++17)

A from-scratch HTTP server built in C++17 using POSIX sockets. The goal is to learn core networking, concurrency, and HTTP fundamentals without third-party libraries.

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

## Project Structure

- `src/` - C++ source files
- `include/` - Header files
- `www/` - Web root for static files (Phase 3)
- `docs/` - Learning notes and phase write-ups

## Roadmap (Phases)

1. Phase 0 - Project scaffold and build workflow (done)
2. Phase 1 - TCP Hello World server (done)
3. Phase 2 - Parse HTTP requests
4. Phase 3 - Serve static files
5. Phase 4 - Thread-per-connection
6. Phase 5 - Thread pool
7. Phase 6 - Request logging
8. Phase 7 - REST API endpoints
9. Phase 8 - Optimization and benchmarking
>>>>>>> 021a652 (Phase 1: TCP hello world server)
