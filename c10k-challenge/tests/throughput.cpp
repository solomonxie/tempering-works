/*
    $ clang++ -std=c++20 -Wall -Wextra -g c10k-challenge/tests/throughput.cpp -o /tmp/throughput
    $ /tmp/throughput --host 127.0.0.1 --port 8080 --path /small.html --max 10000 --duration 10

    Ramps through concurrency levels (100/1000/5000/10000, capped by --max);
    at each level, `level` keep-alive connections hammer --path back-to-back
    for --duration seconds. Reports req/sec, bytes/sec, and latency
    percentiles per level -- the small-response / large-response workloads
    from Phase 13.
*/

#include "common.hpp"

using namespace c10k;

struct ConnState {
    ResponseReader reader;
    Clock::time_point req_start;
    bool connected = false;
};

void run_level(const sockaddr_in& addr, const std::string& host, const std::string& path,
               int level, double duration_s, Stats& stats) {
    std::string request = build_request(host, path);
    std::vector<pollfd> fds;
    std::unordered_map<int, ConnState> conns;

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
        state.req_start = Clock::now();
        send(pfd.fd, request.data(), request.size(), 0);
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
            state.req_start = Clock::now();
            send(pfd.fd, request.data(), request.size(), 0);
        }
    };

    run_loop(fds, duration_s, on_writable, on_readable, [] {});

    for (auto& pfd : fds) {
        if (pfd.fd >= 0) close(pfd.fd);
    }
}

int main(int argc, char** argv) {
    Args args(argc, argv);
    std::string host = args.get("host", "127.0.0.1");
    int port = args.get_int("port", 8080);
    std::string path = args.get("path", "/small.html");
    int max_conns = args.get_int("max", 10000);
    double duration = args.get_int("duration", 10);

    raise_fd_limit();

    sockaddr_in addr;
    if (!resolve(host, port, addr)) {
        std::cerr << "could not resolve " << host << "\n";
        return 1;
    }

    std::vector<int> levels;
    for (int l : {100, 1000, 5000, 10000}) {
        if (l <= max_conns) levels.push_back(l);
    }
    if (levels.empty()) levels.push_back(max_conns);

    for (int level : levels) {
        std::cout << "== concurrency " << level << "  path=" << path << " ==\n";
        Stats stats;
        run_level(addr, host, path, level, duration, stats);
        stats.print_summary(duration);
    }
    return 0;
}
