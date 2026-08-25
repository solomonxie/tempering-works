/*
    $ clang++ -std=c++20 -Wall -Wextra -g c10k-challenge/tests/mixed_workload.cpp -o /tmp/mixed_workload
    $ /tmp/mixed_workload --host 127.0.0.1 --port 8080 --max 5000 --duration 10

    Like throughput.cpp, but each connection repeatedly picks a random
    resource from a fixed list instead of hammering one fixed path --
    mirrors many clients requesting different resources at once. Reports
    aggregate stats plus a breakdown per resource.
*/

#include "common.hpp"
#include <random>

using namespace c10k;

const std::vector<std::string> kResources = {"/small.html", "/medium.html", "/large.bin"};

struct ConnState {
    ResponseReader reader;
    Clock::time_point req_start;
    bool connected = false;
    std::string current_path;
};

int main(int argc, char** argv) {
    Args args(argc, argv);
    std::string host = args.get("host", "127.0.0.1");
    int port = args.get_int("port", 8080);
    int level = args.get_int("max", 1000);
    double duration = args.get_int("duration", 10);

    raise_fd_limit();

    sockaddr_in addr;
    if (!resolve(host, port, addr)) {
        std::cerr << "could not resolve " << host << "\n";
        return 1;
    }

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> pick(0, kResources.size() - 1);

    std::vector<pollfd> fds;
    std::unordered_map<int, ConnState> conns;
    Stats total;
    std::unordered_map<std::string, Stats> per_resource;
    for (auto& r : kResources) per_resource[r];

    for (int i = 0; i < level; i++) {
        int fd = nonblocking_connect(addr);
        if (fd < 0) {
            total.record_error("connect");
            continue;
        }
        fds.push_back({fd, POLLOUT, 0});
        conns[fd] = ConnState{};
    }

    auto send_next_request = [&](int fd, ConnState& state) {
        state.current_path = kResources[pick(rng)];
        state.req_start = Clock::now();
        std::string request = build_request(host, state.current_path);
        send(fd, request.data(), request.size(), 0);
    };

    auto on_writable = [&](pollfd& pfd) {
        auto& state = conns[pfd.fd];
        if (state.connected) return;
        state.connected = true;
        send_next_request(pfd.fd, state);
        pfd.events = POLLIN;
    };

    auto on_readable = [&](pollfd& pfd) {
        auto& state = conns[pfd.fd];
        char buf[8192];
        int n = recv(pfd.fd, buf, sizeof(buf), 0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n <= 0) {
            total.record_error("recv");
            close(pfd.fd);
            pfd.fd = -1;
            return;
        }
        state.reader.feed(buf, n);
        total.bytes_received += n;
        per_resource[state.current_path].bytes_received += n;
        if (state.reader.done()) {
            double latency = ms_since(state.req_start);
            total.successes++;
            total.record_latency(latency);
            per_resource[state.current_path].successes++;
            per_resource[state.current_path].record_latency(latency);
            state.reader.reset();
            send_next_request(pfd.fd, state);
        }
    };

    run_loop(fds, duration, on_writable, on_readable, [] {});

    for (auto& pfd : fds) {
        if (pfd.fd >= 0) close(pfd.fd);
    }

    std::cout << "== aggregate ==\n";
    total.print_summary(duration);
    for (auto& r : kResources) {
        std::cout << "== " << r << " ==\n";
        per_resource[r].print_summary(duration);
    }
    return 0;
}
