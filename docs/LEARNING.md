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
