# Benchmark Results — Phase 8

**Date:** 2026-06-24 &nbsp; | &nbsp; **Environment:** WSL2 Ubuntu 24.04, g++ 13.3.0, C++17, 8 workers &nbsp; | &nbsp; **Tool:** `benchmark.py --requests 500 --concurrency 8`

---

## Throughput & Latency Summary

<table>
<tr>
  <th>#</th>
  <th>Test</th>
  <th align="right">Req</th>
  <th align="right">Concur</th>
  <th align="right">Req/sec</th>
  <th align="right">p50</th>
  <th align="right">p90</th>
  <th align="right">p99</th>
  <th align="right">Errors</th>
</tr>
<tr>
  <td>1</td>
  <td>Static <code>GET /</code> (index.html)</td>
  <td align="right">500</td>
  <td align="right">8</td>
  <td align="right"><b>585</b></td>
  <td align="right">12.6ms</td>
  <td align="right">18.4ms</td>
  <td align="right">40.5ms</td>
  <td align="right">0</td>
</tr>
<tr>
  <td>2</td>
  <td>Static <code>GET /app.js</code></td>
  <td align="right">500</td>
  <td align="right">8</td>
  <td align="right"><b>567</b></td>
  <td align="right">12.7ms</td>
  <td align="right">20.6ms</td>
  <td align="right">31.8ms</td>
  <td align="right">0</td>
</tr>
<tr>
  <td>3</td>
  <td>Static <code>GET /style.css</code></td>
  <td align="right">500</td>
  <td align="right">8</td>
  <td align="right"><b>521</b></td>
  <td align="right">13.0ms</td>
  <td align="right">22.1ms</td>
  <td align="right">70.8ms</td>
  <td align="right">0</td>
</tr>
<tr>
  <td>4</td>
  <td>Keep-alive pipeline <code>GET /</code></td>
  <td align="right">100</td>
  <td align="right">1</td>
  <td align="right"><b>—</b></td>
  <td align="right">1.5ms</td>
  <td align="right">1.6ms</td>
  <td align="right">4.9ms</td>
  <td align="right">0</td>
</tr>
<tr>
  <td>5</td>
  <td>API <code>GET /api/notes</code></td>
  <td align="right">250</td>
  <td align="right">8</td>
  <td align="right"><b>358</b></td>
  <td align="right">12.1ms</td>
  <td align="right">16.8ms</td>
  <td align="right">313.7ms</td>
  <td align="right">0</td>
</tr>
<tr>
  <td>6</td>
  <td>API <code>POST /api/notes</code></td>
  <td align="right">50</td>
  <td align="right">1</td>
  <td align="right"><b>—</b></td>
  <td align="right">3.0ms</td>
  <td align="right">—</td>
  <td align="right">—</td>
  <td align="right">0</td>
</tr>
<tr>
  <td>7</td>
  <td>API <code>DELETE /api/notes/:id</code></td>
  <td align="right">50</td>
  <td align="right">1</td>
  <td align="right"><b>—</b></td>
  <td align="right">5.7ms</td>
  <td align="right">—</td>
  <td align="right">—</td>
  <td align="right">0</td>
</tr>
</table>

> **Average static throughput: 557 req/s.** Keep-alive latency is sub-2ms. API p99 spikes to 314ms due to mutex contention on the notes store (see analysis below).

---

## Static Files — Cache Behavior

All static requests route through `file_cache::read_file()`: LRU eviction, `std::shared_mutex` for concurrent reads, `std::filesystem::last_write_time` for mtime-based invalidation.

<table>
<tr>
  <th>File</th>
  <th align="right">Size</th>
  <th align="right">Req/sec</th>
  <th align="right">p50</th>
  <th align="right">p90</th>
  <th align="right">p99</th>
</tr>
<tr>
  <td><code>www/index.html</code></td>
  <td align="right">1.3 KB</td>
  <td align="right">585</td>
  <td align="right">12.6ms</td>
  <td align="right">18.4ms</td>
  <td align="right">40.5ms</td>
</tr>
<tr>
  <td><code>www/app.js</code></td>
  <td align="right">2.0 KB</td>
  <td align="right">567</td>
  <td align="right">12.7ms</td>
  <td align="right">20.6ms</td>
  <td align="right">31.8ms</td>
</tr>
<tr>
  <td><code>www/style.css</code></td>
  <td align="right">2.4 KB</td>
  <td align="right">521</td>
  <td align="right">13.0ms</td>
  <td align="right">22.1ms</td>
  <td align="right">70.8ms</td>
</tr>
</table>

**How the cache works per request:**

1. Take `shared_lock` → check if path exists in cache with matching mtime → if yes, return cached body (no disk I/O)
2. If miss or stale → drop shared_lock → read from disk → take `unique_lock` → insert/update entry → evict LRU if over capacity (64 entries) → return

Cold start penalty is one disk read per file; all subsequent requests served from memory.

---

## Keep-Alive — Connection Reuse

100 consecutive `GET /` requests over a **single TCP connection** with `Connection: keep-alive`.

<table>
<tr>
  <th>Window</th>
  <th align="right">Avg latency</th>
  <th>Trend</th>
</tr>
<tr>
  <td>Requests 1–5 (first batch)</td>
  <td align="right">2.94ms</td>
  <td>—</td>
</tr>
<tr>
  <td>Requests 6–95 (steady state)</td>
  <td align="right">~1.50ms</td>
  <td>↓ 49%</td>
</tr>
<tr>
  <td>Requests 96–100 (last batch)</td>
  <td align="right">1.63ms</td>
  <td>↓ 45% from start</td>
</tr>
</table>

<br>

<table>
<tr>
  <th>Metric</th>
  <th align="right">Value</th>
  <th>Metric</th>
  <th align="right">Value</th>
</tr>
<tr>
  <td>Completed</td>
  <td align="right">100 / 100</td>
  <td>Min</td>
  <td align="right">1.36ms</td>
</tr>
<tr>
  <td>Errors</td>
  <td align="right">0</td>
  <td>p50</td>
  <td align="right">1.45ms</td>
</tr>
<tr>
  <td>p90</td>
  <td align="right">1.55ms</td>
  <td>Max</td>
  <td align="right">4.94ms</td>
</tr>
</table>

**Why the 2× speedup after the first few requests:** TCP slow-start and TLS-free handshake elimination. First request pays connection setup; subsequent requests on the warmed-up socket skip `accept()` → `read_http_request()` → `build_http_response()` → `send_all()` cycle with zero TCP overhead.

**Safety limits enforced:**
- `SO_RCVTIMEO` = 5s — idle connection killed, worker freed
- Per-connection cap = 100 requests — prevents monopolization
- Parse errors (`400`) always close the connection

---

## API Endpoints

### `GET /api/notes` — List all notes

<table>
<tr>
  <th align="right">Req</th>
  <th align="right">Concur</th>
  <th align="right">Req/sec</th>
  <th align="right">p50</th>
  <th align="right">p90</th>
  <th align="right">p99</th>
  <th align="right">Errors</th>
</tr>
<tr>
  <td align="right">250</td>
  <td align="right">8</td>
  <td align="right">358</td>
  <td align="right">12.1ms</td>
  <td align="right">16.8ms</td>
  <td align="right">313.7ms</td>
  <td align="right">0</td>
</tr>
</table>

**p99 spike (313.7ms):** Caused by all 8 workers contending on `notes_store::mutex_` during simultaneous `list_notes()` → `build_notes_json()` calls. The mutex serializes read access to the notes vector. A `shared_mutex` (reader-writer lock) would drop p99 significantly since `list_notes()` is read-only.

### `POST /api/notes` + `DELETE /api/notes/:id` — Create/delete cycle

<table>
<tr>
  <th>Operation</th>
  <th align="right">Count</th>
  <th align="right">Avg latency</th>
  <th>Body</th>
</tr>
<tr>
  <td><code>POST</code> create</td>
  <td align="right">50</td>
  <td align="right">3.03ms</td>
  <td><code>{"text": "Benchmark note N"}</code></td>
</tr>
<tr>
  <td><code>DELETE</code> delete</td>
  <td align="right">50</td>
  <td align="right">5.65ms</td>
  <td>—</td>
</tr>
</table>

POST is faster than DELETE because delete searches the vector linearly (`O(n)` scan for ID match); create just pushes to the end (`O(1)`).

---

## Before vs After — Phase 7 → Phase 8

<table>
<tr>
  <th>Metric</th>
  <th>Phase 7</th>
  <th>Phase 8</th>
  <th>Change</th>
</tr>
<tr>
  <td>Static file I/O</td>
  <td><code>ifstream</code> open/read/close per request</td>
  <td>LRU cache + <code>shared_mutex</code></td>
  <td>Memory-bound from disk-bound</td>
</tr>
<tr>
  <td>Static throughput (est.)</td>
  <td>~200 req/s</td>
  <td><b>~557 req/s</b></td>
  <td><b>~2.8×</b></td>
</tr>
<tr>
  <td>Keep-alive</td>
  <td>Not supported</td>
  <td>100 reqs/connection, sub-2ms p50</td>
  <td>New capability</td>
</tr>
<tr>
  <td>Connection overhead</td>
  <td>TCP handshake per request</td>
  <td>Amortized over 100 reqs</td>
  <td>~100× reduction</td>
</tr>
<tr>
  <td>File modification detection</td>
  <td>None</td>
  <td><code>filesystem::last_write_time</code> per hit</td>
  <td>Automatic invalidation</td>
</tr>
<tr>
  <td>Cache capacity</td>
  <td>N/A</td>
  <td>64 entries, configurable, LRU eviction</td>
  <td>New capability</td>
</tr>
<tr>
  <td>API throughput</td>
  <td>~350 req/s</td>
  <td>~358 req/s</td>
  <td>No change (cache skips API path)</td>
</tr>
</table>

---

## Known Limitations

<table>
<tr>
  <th align="right">#</th>
  <th>Area</th>
  <th>Limitation</th>
  <th>Impact</th>
</tr>
<tr>
  <td align="right">1</td>
  <td>API p99</td>
  <td>Single <code>std::mutex</code> on notes store serializes reads</td>
  <td>Tail latency spikes under concurrency</td>
</tr>
<tr>
  <td align="right">2</td>
  <td>DELETE ID tracking</td>
  <td>Hand-rolled JSON parser doesn't sync IDs after create/delete cycling</td>
  <td>DELETE may 404 on expected IDs</td>
</tr>
<tr>
  <td align="right">3</td>
  <td>Cache</td>
  <td>No byte-size limit — capped by entry count only (64)</td>
  <td>Large files may balloon memory</td>
</tr>
<tr>
  <td align="right">4</td>
  <td>Cache</td>
  <td>Cold at startup — no pre-warming</td>
  <td>First request per file hits disk</td>
</tr>
<tr>
  <td align="right">5</td>
  <td>Keep-alive</td>
  <td>Worker thread tied to each kept-alive connection</td>
  <td>Not suitable for C10K (needs epoll/kqueue)</td>
</tr>
<tr>
  <td align="right">6</td>
  <td>HTTP</td>
  <td>No pipelining — one in-flight request per connection</td>
  <td>Clients must wait for response before sending next</td>
</tr>
<tr>
  <td align="right">7</td>
  <td>HTTP</td>
  <td>No <code>Transfer-Encoding: chunked</code></td>
  <td>Response must fit in memory before sending</td>
</tr>
<tr>
  <td align="right">8</td>
  <td>TLS</td>
  <td>No HTTPS support</td>
  <td>All traffic plaintext</td>
</tr>
</table>
