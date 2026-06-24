# Benchmark Results — Phase 8

**Date:** 2026-06-24 &nbsp; | &nbsp; **Environment:** WSL2 Ubuntu 24.04, g++ 13.3.0, C++17, 8 workers &nbsp; | &nbsp; **Tool:** `benchmark.py --requests 500 --concurrency 8`

---

## Summary

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

---

## Static — File Cache (LRU + shared_mutex)

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

---

## Keep-Alive — Connection Reuse

100 consecutive `GET /` requests over a single TCP connection.

<table>
<tr>
  <th>Window</th>
  <th align="right">Avg latency</th>
</tr>
<tr>
  <td>Requests 1–5 (first batch)</td>
  <td align="right">2.94ms</td>
</tr>
<tr>
  <td>Requests 6–95 (steady state)</td>
  <td align="right">~1.50ms</td>
</tr>
<tr>
  <td>Requests 96–100 (last batch)</td>
  <td align="right">1.63ms</td>
</tr>
</table>

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
  <td>p50</td>
  <td align="right">1.45ms</td>
  <td>p90</td>
  <td align="right">1.55ms</td>
</tr>
<tr>
  <td>Max</td>
  <td align="right">4.94ms</td>
  <td>Errors</td>
  <td align="right">0</td>
</tr>
</table>

---

## API

### `GET /api/notes`

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

### `POST /api/notes` + `DELETE /api/notes/:id`

<table>
<tr>
  <th>Operation</th>
  <th align="right">Count</th>
  <th align="right">Avg latency</th>
</tr>
<tr>
  <td><code>POST</code> create</td>
  <td align="right">50</td>
  <td align="right">3.03ms</td>
</tr>
<tr>
  <td><code>DELETE</code> delete</td>
  <td align="right">50</td>
  <td align="right">5.65ms</td>
</tr>
</table>
