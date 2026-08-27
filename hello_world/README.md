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
    - epoll architecture refinement at scale (thousands of fds, not the hello_world handful)
    - Buffer management
    - Memory allocation reduction
    - System-call reduction
    - Thread/event-loop architecture
- Phase 15 — HTTP features
- Phase 16 — Robustness
- Phase 17 — Testing
- Phase 18 — HTTPS

## Phase 10 — Non-blocking server: detailed steps

Each step below is its own file (unlike phases 1-9, which each grew one shared file).

- Step 1 — `hello_09_poll.cpp` — `poll()` watches `server_fd` for `POLLIN` (data/connection ready)
- Step 2 — `hello_10_POLLIN.cpp` — iterate `fds[]`, only act when `revents & POLLIN` is set
- Step 3 — `hello_11_poll_vector.cpp` — swap fixed `pollfd fds[1]` array for `std::vector<pollfd>` so more sockets can be added
- Step 4 — `hello_12_accept_loop.cpp` — loop distinguishes `server_fd` (accept) vs client fds (handle); new client fds get pushed into `fds`
- Step 5 — `hello_13_fcntl.cpp` — `fcntl(client_fd, O_NONBLOCK)`; `handle_client()` rewritten to do one `recv`+`send` per call and return a `keep_alive` bool, instead of looping until the connection ends, so one client can't hog the thread
- Step 6 — `hello_14_EAGAIN.cpp` — detect `EAGAIN`/`EWOULDBLOCK` on `recv()` and `send()` and treat it as "try again later", not a real close/error
- Step 7 — `hello_15_POLLHUP.cpp` — check `accept()`'s return value and skip on failure (`-1`) instead of pushing a bad fd into `fds`; also react to `POLLHUP`/`POLLERR`, not just `POLLIN`, so a reset/half-closed client gets cleaned up instead of leaking its fd
- Step 8 — `hello_16_partial_write.cpp` — buffer partial `send()` writes (`PendingWrite`/`try_send()`) and switch the fd to `POLLOUT` to finish sending on the next writable event, instead of silently dropping the remainder; the loop resumes a queued write before handling new `POLLIN` reads
- Step 9 — `hello_17_partial_read.cpp` — buffer partial/multi-part `recv()` reads (`pending_reads`, keyed by fd) until the header terminator `\r\n\r\n` shows up, so a request split across TCP segments still parses correctly instead of being handled half-arrived
- Step 10 — `hello_18_epoll.cpp` — swap `poll()`/`std::vector<pollfd>` for `epoll_create1()`/`epoll_ctl()`/`epoll_wait()` (level-triggered); same behavior as step 9, but the kernel tracks the watch list and only returns fds that are actually ready, instead of the caller scanning the whole array every call
- Step 11 — `hello_19_epoll_edge_triggered.cpp` — switch to `EPOLLET` (edge-triggered); `accept()` and `recv()` now loop until `EAGAIN` instead of once per wakeup, since an edge-triggered fd only notifies once per level change
- Step 12 — `hello_20_accept4.cpp` — `accept4(..., SOCK_NONBLOCK)` replaces `accept()` + `fcntl(O_NONBLOCK)`, folding two syscalls (and the brief window between them) into one
- Step 13 — `hello_21_sendfile.cpp` — `sendfile()` replaces reading a file into a `std::string` and `send()`-ing that; the body's bytes move kernel-to-kernel (page cache to socket) instead of round-tripping through a userspace buffer; tracked via `PendingFile` so a partial `sendfile()` resumes on the next writable event, same idea as `PendingWrite`

Steps 10-13 are Linux-only (`epoll`, `accept4()`, `sendfile()` don't exist on macOS/BSD) — compile and run them on the project's EC2 box, not locally.
