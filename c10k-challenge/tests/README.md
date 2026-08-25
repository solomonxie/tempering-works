Load-test clients for the C10K server (see `../TESTING_PLAN.md` for the
design). Point any of them at a running server with `--host`/`--port`.

## Build

```
$ clang++ -std=c++20 -Wall -Wextra -g c10k-challenge/tests/<tool>.cpp -o /tmp/<tool>
```

Each tool builds standalone (only shares `common.hpp` via `#include`); no
separate build system.

## Before a full 10K run

Static resources under `../static/` are what the tools request:
`small.html` and `medium.html` are committed; `large.bin` (~5MB) is
generated locally and gitignored:

```
$ c10k-challenge/static/generate_large_file.sh
```

Every tool calls `raise_fd_limit()` on startup to bump its own client-side
fd ceiling, so a 10,000-connection run doesn't need a manual `ulimit -n`
first. The **server** process being tested needs the same treatment once it
exists -- `raise_fd_limit()` in `common.hpp` is a ready-made reference for
that, or just run the server after `ulimit -n 20000` in its own shell.

## Tools

### `conn_flood`
Ramps concurrent *idle* connections in steps, holding each open without
sending data. Reports, per step, how many opened vs. failed and how long
the batch took. Finds the server's actual concurrent-connection ceiling.

```
$ /tmp/conn_flood --host 127.0.0.1 --port 8080 --max 10000 [--step N]
```
`--step` sets a fixed linear increment; omit it for a doubling ramp
(100, 200, 400, ... `--max`).

### `throughput`
Ramps through concurrency levels (100/1000/5000/10000, capped by `--max`);
at each level, that many keep-alive connections hammer one `--path`
back-to-back for `--duration` seconds. Reports req/sec, bytes, and latency
percentiles per level.

```
$ /tmp/throughput --host 127.0.0.1 --port 8080 --path /small.html --max 10000 --duration 10
```
Point `--path` at `/small.html` or `/large.bin` for the small- vs
large-response workloads.

### `mixed_workload`
Like `throughput`, but each connection repeatedly picks a random resource
from `/small.html`, `/medium.html`, `/large.bin` instead of one fixed path.
Reports aggregate stats plus a per-resource breakdown.

```
$ /tmp/mixed_workload --host 127.0.0.1 --port 8080 --max 5000 --duration 10
```

### `keepalive_scale`
Opens `--max` persistent connections that stay mostly idle, each sending
one request every `--interval` seconds over `--duration` seconds total.
Reports how many connections survive the full run and the latency of the
periodic requests -- keep-alive at scale with slow/idle clients.

```
$ /tmp/keepalive_scale --host 127.0.0.1 --port 8080 --path /small.html --max 2000 --interval 5 --duration 60
```

## Common flags

`--host` (default `127.0.0.1`), `--port` (default `8080`), `--max`
(concurrency ceiling), `--duration` (seconds, where applicable), `--path`
(resource to request, where applicable).
