/*
    $ clang++ -std=c++20 -Wall -Wextra -g c10k-challenge/tests/keepalive_scale.cpp -o /tmp/keepalive_scale
    $ /tmp/keepalive_scale --host 127.0.0.1 --port 8080 --max 2000 --interval 5 --duration 60

    Opens --max persistent connections that stay mostly idle, each sending
    one request every --interval seconds over --duration seconds total.
    Reports how many connections survive the full run without an
    error/timeout/reset, plus latency of the periodic requests -- Phase 12's
    "keep-alive at scale" and "slow clients".
*/

#include "common.hpp"

using namespace c10k;

struct ConnState {
    ResponseReader reader;
    Clock::time_point req_start;
    Clock::time_point next_due;
    bool connected = false;
    bool awaiting_response = false;
};

int main(int argc, char** argv) {
    Args args(argc, argv);
    std::string host = args.get("host", "127.0.0.1");
    int port = args.get_int("port", 8080);
    std::string path = args.get("path", "/small.html");
    int level = args.get_int("max", 2000);
    double interval = args.get_int("interval", 5);
    double duration = args.get_int("duration", 60);

    raise_fd_limit();

    sockaddr_in addr;
    if (!resolve(host, port, addr)) {
        std::cerr << "could not resolve " << host << "\n";
        return 1;
    }

    std::string request = build_request(host, path);
    std::vector<pollfd> fds;
    std::unordered_map<int, ConnState> conns;
    Stats stats;

    for (int i = 0; i < level; i++) {
        int fd = nonblocking_connect(addr);
        if (fd < 0) {
            stats.record_error("connect");
            continue;
        }
        fds.push_back({fd, POLLOUT, 0});
        conns[fd] = ConnState{};
    }

    auto on_writable = [&](pollfd& pfd) {
        auto& state = conns[pfd.fd];
        if (state.connected) return;
        state.connected = true;
        state.next_due = Clock::now();  // fire the first request right away
        pfd.events = POLLIN;
    };

    auto on_readable = [&](pollfd& pfd) {
        auto& state = conns[pfd.fd];
        char buf[8192];
        int n = recv(pfd.fd, buf, sizeof(buf), 0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n <= 0) {
            stats.record_error("recv");
            close(pfd.fd);
            pfd.fd = -1;
            return;
        }
        state.reader.feed(buf, n);
        stats.bytes_received += n;
        if (state.reader.done()) {
            stats.successes++;
            stats.record_latency(ms_since(state.req_start));
            state.reader.reset();
            state.awaiting_response = false;
            state.next_due = Clock::now() +
                std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(interval));
        }
    };

    auto on_tick = [&] {
        auto now = Clock::now();
        for (auto& pfd : fds) {
            if (pfd.fd < 0) continue;
            auto& state = conns[pfd.fd];
            if (!state.connected || state.awaiting_response) continue;
            if (now >= state.next_due) {
                state.awaiting_response = true;
                state.req_start = now;
                send(pfd.fd, request.data(), request.size(), 0);
            }
        }
    };

    run_loop(fds, duration, on_writable, on_readable, on_tick);

    std::cout << "connections still alive at end: " << fds.size() << " / " << level << "\n";
    stats.print_summary(duration);

    for (auto& pfd : fds) {
        if (pfd.fd >= 0) close(pfd.fd);
    }
    return 0;
}
