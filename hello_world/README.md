Goal: implement a C++ web server from scratch through progressive phases.
Phases of implementation.

Basic phases:
- Phase 0 — C++ basics
- Phase 1 — Hello World
- Phase 2 — TCP server
- Phase 3 — First HTTP response
- Phase 4 — HTTP request parser
- Phase 5 — HTTP response abstraction
- Phase 6 — Routing
- Phase 7 — Serving files
- Phase 8 — Multiple clients
- Phase 9 — Persistent connections
- Phase 10 — Non-blocking server

Then C10K-focused:
- Phase 11 — C10K foundations
    - Why blocking/thread-per-connection struggles
    - File descriptors
    - Event loops
    - epoll
    - Readiness notifications
    - Connection state machines
    - Handling thousands of sockets with limited threads
- Phase 12 — C10K HTTP server
    - Thousands of simultaneous connections
    - Partial reads/writes
    - Slow clients
    - Keep-alive at scale
    - Per-connection memory
    - Efficient buffers
    - Avoiding unnecessary copies
    - Connection limits
- Phase 13 — C10K benchmarking
    - Load generation
    - 1K → 5K → 10K connections
    - Static-file workload
    - Small-response workload
    - Large-response workload
    - Different clients requesting different resources
    - CPU, memory, latency, throughput
    - Finding bottlenecks
- Phase 14 — C10K optimization
    - epoll architecture refinement
    - EPOLLIN / EPOLLOUT
    - Edge-triggered vs level-triggered
    - accept4()
    - sendfile()
    - Buffer management
    - Memory allocation reduction
    - System-call reduction
    - Thread/event-loop architecture
- Phase 15 — HTTP features
- Phase 16 — Robustness
- Phase 17 — Testing
- Phase 18 — HTTPS

## Phase 10 — Non-blocking server: detailed steps

- Step 1 — `poll()` watches `server_fd` for `POLLIN` (data/connection ready)
- Step 2 — iterate `fds[]`, only act when `revents & POLLIN` is set
- Step 3 — swap fixed `pollfd fds[1]` array for `std::vector<pollfd>` so more sockets can be added
- Step 4 — loop distinguishes `server_fd` (accept) vs client fds (handle); new client fds get pushed into `fds`
- Step 5 — `fcntl(client_fd, O_NONBLOCK)`; `handle_client()` rewritten to do one `recv`+`send` per call and return a `keep_alive` bool, instead of looping until the connection ends, so one client can't hog the thread
- Step 6 — detect `EAGAIN`/`EWOULDBLOCK` on `recv()` and `send()` and treat it as "try again later", not a real close/error
- Step 7 — event-loop robustness: check `accept()`'s return value and skip on failure (`-1`) instead of pushing a bad fd into `fds`; also react to `POLLHUP`/`POLLERR`, not just `POLLIN`, so a reset/half-closed client gets cleaned up instead of leaking its fd
- Step 8 — buffer partial `send()` writes and re-arm `POLLOUT` to finish sending instead of silently dropping the remainder
- Step 9 — buffer partial/multi-part `recv()` reads so a request bigger than one buffer, or split across TCP segments, still parses correctly
