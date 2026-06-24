#!/usr/bin/env python3
"""
Phase 8 benchmark tool for the multi-threaded HTTP server.

Usage:
  python benchmark.py [--host HOST] [--port PORT] [--requests N] [--concurrency C]

Tests:
  1. Static file throughput (cache-sensitive)
  2. API list notes (JSON serialization)
  3. Keep-alive pipelining (connection reuse)
  4. Mixed workload (static + API)

Reports requests/sec and latency percentiles for each test.
"""

import argparse
import http.client
import json
import sys
import time
import threading
from collections import defaultdict


def now_ms():
    return time.perf_counter() * 1000


def fetch_static(client_class, host, port, path):
    """Fetch a static file. Returns (status, latency_ms)."""
    start = now_ms()
    try:
        conn = client_class(host, port, timeout=10)
        conn.request("GET", path)
        resp = conn.getresponse()
        resp.read()  # drain body
        elapsed = now_ms() - start
        conn.close()
        return resp.status, elapsed
    except Exception as e:
        elapsed = now_ms() - start
        return 0, elapsed


def fetch_static_keepalive(host, port, path, count):
    """Fetch the same path N times over one keep-alive connection. Returns list of (status, latency_ms)."""
    results = []
    conn = http.client.HTTPConnection(host, port, timeout=10)
    conn.connect()
    try:
        for _ in range(count):
            start = now_ms()
            conn.request("GET", path, headers={"Connection": "keep-alive"})
            resp = conn.getresponse()
            resp.read()  # drain body
            elapsed = now_ms() - start
            results.append((resp.status, elapsed))
    finally:
        conn.close()
    return results


def fetch_api(client_class, host, port, method, path, body=None):
    """Make an API request. Returns (status, latency_ms)."""
    start = now_ms()
    try:
        conn = client_class(host, port, timeout=10)
        headers = {"Content-Type": "application/json"}
        conn.request(method, path, body=body, headers=headers)
        resp = conn.getresponse()
        resp.read()
        elapsed = now_ms() - start
        conn.close()
        return resp.status, elapsed
    except Exception as e:
        elapsed = now_ms() - start
        return 0, elapsed


def percentile(sorted_vals, p):
    if not sorted_vals:
        return 0
    idx = int(len(sorted_vals) * p / 100)
    return sorted_vals[min(idx, len(sorted_vals) - 1)]


def run_test(name, func, total, concurrency):
    """Run a test with N total requests and C concurrent workers. Returns stats dict."""
    print(f"\n{'='*60}")
    print(f"Test: {name}")
    print(f"Requests: {total}  Concurrency: {concurrency}")
    print(f"{'='*60}")

    results_lock = threading.Lock()
    results = []
    errors = []
    start_barrier = threading.Barrier(concurrency) if concurrency > 1 else None

    def worker(worker_id, count):
        if start_barrier:
            start_barrier.wait()
        for _ in range(count):
            status, latency = func()
            with results_lock:
                if status == 0:
                    errors.append(1)
                else:
                    results.append(latency)

    threads = []
    per_worker = total // concurrency
    remainder = total % concurrency
    for i in range(concurrency):
        cnt = per_worker + (1 if i < remainder else 0)
        t = threading.Thread(target=worker, args=(i, cnt))
        threads.append(t)

    overall_start = now_ms()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    overall_elapsed = (now_ms() - overall_start) / 1000.0

    completed = len(results)
    failed = len(errors)
    total_done = completed + failed
    rps = completed / overall_elapsed if overall_elapsed > 0 else 0

    sorted_lat = sorted(results)
    print(f"  Completed: {completed}/{total_done}")
    print(f"  Errors:    {failed}")
    print(f"  Time:      {overall_elapsed:.2f}s")
    print(f"  Req/sec:   {rps:.1f}")
    print(f"  Latency (ms):")
    print(f"    min:    {min(sorted_lat):.2f}" if sorted_lat else "    min:    N/A")
    print(f"    p50:    {percentile(sorted_lat, 50):.2f}")
    print(f"    p75:    {percentile(sorted_lat, 75):.2f}")
    print(f"    p90:    {percentile(sorted_lat, 90):.2f}")
    print(f"    p99:    {percentile(sorted_lat, 99):.2f}")
    print(f"    max:    {max(sorted_lat):.2f}" if sorted_lat else "    max:    N/A")

    return {
        "name": name,
        "completed": completed,
        "errors": failed,
        "time_s": overall_elapsed,
        "rps": rps,
        "p50": percentile(sorted_lat, 50),
        "p90": percentile(sorted_lat, 90),
        "p99": percentile(sorted_lat, 99),
    }


def main():
    parser = argparse.ArgumentParser(description="Benchmark the HTTP server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--requests", type=int, default=1000)
    parser.add_argument("--concurrency", type=int, default=8)
    args = parser.parse_args()

    host = args.host
    port = args.port
    total = args.requests
    conc = args.concurrency

    print(f"Benchmarking http://{host}:{port}")
    print(f"Total requests per test: {total}")
    print(f"Concurrency: {conc}")

    all_stats = []

    # Test 1: Static file serving (index.html) — cache-sensitive
    all_stats.append(run_test(
        "Static GET / (index.html)",
        lambda: fetch_static(http.client.HTTPConnection, host, port, "/"),
        total, conc
    ))

    # Test 2: Static file serving (app.js) — different file, exercises cache
    all_stats.append(run_test(
        "Static GET /app.js",
        lambda: fetch_static(http.client.HTTPConnection, host, port, "/app.js"),
        total, conc
    ))

    # Test 3: Static file serving (style.css)
    all_stats.append(run_test(
        "Static GET /style.css",
        lambda: fetch_static(http.client.HTTPConnection, host, port, "/style.css"),
        total, conc
    ))

    # Test 4: Keep-alive pipelining (reuse one connection for many requests)
    print(f"\n{'='*60}")
    print(f"Test: Keep-Alive pipeline (100 reqs on one connection)")
    print(f"{'='*60}")
    ka_results = fetch_static_keepalive(host, port, "/", 100)
    ka_latencies = [l for s, l in ka_results if s == 200]
    ka_errors = len([s for s, l in ka_results if s != 200])
    print(f"  Completed: {len(ka_latencies)}/100")
    print(f"  Errors:    {ka_errors}")
    if ka_latencies:
        print(f"  Latency (ms): min={min(ka_latencies):.2f} p50={percentile(ka_latencies, 50):.2f} "
              f"p90={percentile(ka_latencies, 90):.2f} max={max(ka_latencies):.2f}")
        # Compare last vs first to see if keep-alive helps
        if len(ka_latencies) >= 10:
            first_avg = sum(ka_latencies[:5]) / 5
            last_avg = sum(ka_latencies[-5:]) / 5
            print(f"  First 5 avg: {first_avg:.2f}ms  Last 5 avg: {last_avg:.2f}ms")

    # Test 5: API list notes
    all_stats.append(run_test(
        "API GET /api/notes",
        lambda: fetch_api(http.client.HTTPConnection, host, port, "GET", "/api/notes"),
        total // 2, conc  # fewer requests for API
    ))

    # Test 6: API create + delete cycle
    print(f"\n{'='*60}")
    print(f"Test: API POST + DELETE cycle")
    print(f"{'='*60}")
    create_times = []
    delete_times = []
    for i in range(min(100, total // 10)):
        # Create
        body = json.dumps({"text": f"Benchmark note {i}"})
        status, lat = fetch_api(http.client.HTTPConnection, host, port, "POST", "/api/notes", body)
        if status == 201:
            create_times.append(lat)
        # Delete — note ID starts at 1 and increments; we approximate
        status, lat = fetch_api(http.client.HTTPConnection, host, port, "DELETE", f"/api/notes/{i+1}")
        if status == 204:
            delete_times.append(lat)
    print(f"  Creates: {len(create_times)}  avg latency: {sum(create_times)/len(create_times):.2f}ms" if create_times else "  Creates: 0")
    print(f"  Deletes: {len(delete_times)}  avg latency: {sum(delete_times)/len(delete_times):.2f}ms" if delete_times else "  Deletes: 0")

    # Summary
    print(f"\n{'='*60}")
    print("SUMMARY")
    print(f"{'='*60}")
    print(f"{'Test':<30} {'Req/s':>8} {'p50(ms)':>9} {'p90(ms)':>9} {'p99(ms)':>9}")
    print("-" * 66)
    for s in all_stats:
        print(f"{s['name']:<30} {s['rps']:>8.1f} {s['p50']:>9.2f} {s['p90']:>9.2f} {s['p99']:>9.2f}")


if __name__ == "__main__":
    main()
