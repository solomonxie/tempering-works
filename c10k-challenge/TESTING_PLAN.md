# C10K testing plan

`hello_world/` walked a C++ HTTP server up through Phase 10
(`hello_9_nonblocking.cpp`: `poll()`-based, non-blocking, single thread).
The roadmap in `hello_world/README.md` turns to C10K next (Phases 11–14:
foundations, the C10K server itself, benchmarking, optimization).

Before writing that server, this folder gets its test harness ready first:
C++ load-generating clients that can drive a server up to 10,000 concurrent
connections, plus the static resources those tests request. These become
the acceptance bar the new server is built against — write the server, then
point these clients at it.

## Layout

```
c10k-challenge/
  README.md
  TESTING_PLAN.md            (this file)
  static/
    small.html               (~1KB   — small-response workload)
    medium.html              (~50KB  — mid-size variety for mixed workload)
    generate_large_file.sh   (creates static/large.bin, ~5MB, gitignored — large-response workload)
  tests/
    README.md
    common.hpp                (shared: nonblocking connect, poll() event loop, stats/percentiles, HTTP build/parse, fd-limit raise)
    conn_flood.cpp             (ramp idle concurrent connections)
    throughput.cpp             (fixed-duration request-rate + latency)
    mixed_workload.cpp         (many clients, varied resources)
    keepalive_scale.cpp        (many persistent, mostly-idle connections)
```

Each `.cpp` is self-contained with a compile/run comment at the top (same
convention as `hello_world/hello_*.cpp` — no new Makefile/build system
introduced). `static/large.bin` is generated locally and gitignored rather
than committed.

## `common.hpp` — shared client building blocks

- `raise_fd_limit()`: `getrlimit`/`setrlimit(RLIMIT_NOFILE, ...)` bumps the
  test process's own soft fd limit to its hard limit at startup, so hitting
  10,000 sockets doesn't require the user to remember `ulimit -n` first;
  prints before/after counts.
- `nonblocking_connect(host, port)`: creates a socket, `fcntl(O_NONBLOCK)`,
  kicks off `connect()`, returns the fd (completion confirmed via `poll()`'s
  `POLLOUT` in the caller's event loop) — mirrors the accept-side pattern
  already used in `hello_9_nonblocking.cpp`.
- A minimal HTTP/1.1 request builder
  (`GET <path> HTTP/1.1\r\nHost: ...\r\nConnection: keep-alive\r\n\r\n`) and
  a response reader that tracks `Content-Length`/`\r\n\r\n` to know when one
  response has fully arrived on a non-blocking socket.
- `Stats`: records per-request latency samples plus success/error counts;
  computes p50/p90/p99 and req/sec; a `print_summary()` for consistent
  console output across all four tools.
- A small manual argv parser (`--host`, `--port`, `--path`, `--max`,
  `--duration`, `--step`) — no CLI-parsing dependency, matching the repo's
  existing minimal style.

## The four test tools

1. **`conn_flood.cpp`** — ramps concurrent *idle* connections in steps
   (e.g. 100 → 500 → 1000 → ... → 10,000, `--max`/`--step` configurable),
   holding each open without sending data, using a `poll()`-driven
   non-blocking loop (same idiom as the server itself). Per step, reports
   attempted/succeeded/failed (with errno breakdown) and time to open the
   batch. Answers: how many simultaneous connections can the server even
   accept and hold?

2. **`throughput.cpp`** — holds a fixed concurrency level (`--max`, default
   ramps through 100/1000/5000/10000) of keep-alive connections, each
   firing repeated requests for `--path` (point at `/small.html` or
   `/large.bin`) for a fixed `--duration`. Reports aggregate req/sec,
   bytes/sec, and latency percentiles. Covers the small-response and
   large-response workloads from Phase 13.

3. **`mixed_workload.cpp`** — like `throughput.cpp`, but each connection
   repeatedly picks a random path from a configurable resource list
   (`small.html`, `medium.html`, `large.bin`) instead of one fixed path,
   and reports both aggregate and per-resource stats. Covers "different
   clients requesting different resources."

4. **`keepalive_scale.cpp`** — opens many persistent connections that stay
   mostly idle, sending one request per connection every few seconds over a
   longer run, and reports how many survive the full duration without
   errors/timeouts/resets. Targets Phase 12's "keep-alive at scale" and
   "slow clients."

## Target resources (`c10k-challenge/static/`)

- `small.html`: short handwritten page (~1KB), same style as
  `hello_world/static/index.html`.
- `medium.html`: a larger but still-committable page (~50KB) for workload
  variety.
- `generate_large_file.sh`: a short shell script (`dd`/`head -c` from
  `/dev/urandom` or a repeated pattern) that writes `static/large.bin`
  (~5MB) locally — kept out of git via `.gitignore` rather than committing
  a multi-MB binary blob.
- No dynamic endpoint is built now (nothing server-side exists yet to
  generate one); `mixed_workload.cpp`'s resource list is written so a
  `/dynamic` path can be added later without restructuring.

## Verification

Since no C10K server exists yet to point these at, verification is that
each tool **compiles cleanly** (`clang++ -std=c++20 -Wall -Wextra -g ...`)
and, smoke-tested against the existing `hello_9_nonblocking.cpp` at a small
scale (e.g. `--max 50`), connects/requests/reports without crashing — this
only exercises the harness itself, not a real 10K run, per the decision not
to baseline the old server now.
