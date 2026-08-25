/*
    Shared building blocks for the c10k-challenge load-test tools: raising
    the fd limit, non-blocking connect, a minimal HTTP/1.1 request/response
    pair, latency stats, argv parsing, and a small poll()-driven event loop.
*/

#pragma once

#include <sys/socket.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <iostream>

namespace c10k {

using Clock = std::chrono::steady_clock;

inline double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Bumps this process's own fd soft limit to its hard limit so opening
// thousands of client sockets doesn't need a manual `ulimit -n` first.
inline void raise_fd_limit() {
    rlimit rl{};
    getrlimit(RLIMIT_NOFILE, &rl);
    rlim_t before = rl.rlim_cur;
    rl.rlim_cur = rl.rlim_max;
    setrlimit(RLIMIT_NOFILE, &rl);
    getrlimit(RLIMIT_NOFILE, &rl);
    std::cout << "fd limit: " << before << " -> " << rl.rlim_cur << "\n";
}

inline bool resolve(const std::string& host, int port, sockaddr_in& addr) {
    addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) == 1) return true;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return true;
}

// Kicks off a non-blocking connect and returns the fd right away; the
// caller's poll() loop watches it for POLLOUT (connected) or
// POLLHUP/POLLERR (failed), mirroring the accept-side pattern the
// server itself will use.
inline int nonblocking_connect(const sockaddr_in& addr) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    fcntl(fd, F_SETFL, O_NONBLOCK);
    int rc = connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    return fd;
}

inline std::string build_request(const std::string& host, const std::string& path) {
    return "GET " + path + " HTTP/1.1\r\n"
           "Host: " + host + "\r\n"
           "Connection: keep-alive\r\n"
           "\r\n";
}

// Accumulates bytes of one HTTP response as they arrive on a non-blocking
// socket; done() once the full body has been read, using Content-Length.
struct ResponseReader {
    std::string buf;
    long content_length = -1;
    size_t header_end = std::string::npos;

    void feed(const char* data, size_t n) {
        buf.append(data, n);
        if (header_end == std::string::npos) {
            auto pos = buf.find("\r\n\r\n");
            if (pos != std::string::npos) {
                header_end = pos + 4;
                content_length = parse_content_length(buf.substr(0, header_end));
            }
        }
    }

    bool done() const {
        if (header_end == std::string::npos) return false;
        if (content_length < 0) return true;  // no Content-Length: headers-only counts as done
        return buf.size() >= header_end + static_cast<size_t>(content_length);
    }

    void reset() {
        buf.clear();
        content_length = -1;
        header_end = std::string::npos;
    }

    static long parse_content_length(const std::string& headers) {
        auto pos = headers.find("Content-Length:");
        if (pos == std::string::npos) return -1;
        pos += std::strlen("Content-Length:");
        try {
            return std::stol(headers.substr(pos));
        } catch (...) {
            return -1;
        }
    }
};

struct Stats {
    std::vector<double> latencies_ms;
    long successes = 0;
    long failures = 0;
    long bytes_received = 0;
    std::unordered_map<std::string, long> error_counts;

    void record_latency(double ms) { latencies_ms.push_back(ms); }
    void record_error(const std::string& tag) {
        error_counts[tag]++;
        failures++;
    }

    double percentile(double p) const {
        if (latencies_ms.empty()) return 0.0;
        std::vector<double> sorted = latencies_ms;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(p / 100.0 * (sorted.size() - 1));
        return sorted[idx];
    }

    void print_summary(double duration_s) const {
        std::cout << "  successes: " << successes << "  failures: " << failures
                   << "  bytes: " << bytes_received << "\n";
        if (duration_s > 0) {
            std::cout << "  req/sec: " << (successes / duration_s) << "\n";
        }
        if (!latencies_ms.empty()) {
            std::cout << "  latency ms  p50=" << percentile(50)
                       << "  p90=" << percentile(90)
                       << "  p99=" << percentile(99) << "\n";
        }
        for (auto& [tag, count] : error_counts) {
            std::cout << "  error[" << tag << "]: " << count << "\n";
        }
    }
};

// Manual `--flag value` argv parser -- no CLI-parsing dependency.
struct Args {
    std::unordered_map<std::string, std::string> values;

    Args(int argc, char** argv) {
        for (int i = 1; i + 1 < argc; i += 2) {
            std::string key = argv[i];
            if (key.rfind("--", 0) == 0) {
                values[key.substr(2)] = argv[i + 1];
            }
        }
    }

    std::string get(const std::string& key, const std::string& fallback) const {
        auto it = values.find(key);
        return it == values.end() ? fallback : it->second;
    }

    int get_int(const std::string& key, int fallback) const {
        auto it = values.find(key);
        return it == values.end() ? fallback : std::stoi(it->second);
    }
};

// Drives `fds` with poll() until `duration_s` elapses (<=0 means run until
// `fds` empties out). Per ready fd: POLLHUP/POLLERR closes and drops it;
// otherwise POLLOUT calls on_writable, POLLIN calls on_readable. Both hooks
// signal "drop this fd" by closing it and setting pfd.fd = -1. on_tick runs
// once per wakeup, after fds are processed -- for timer-driven work like
// keepalive_scale's per-connection "think time".
template <typename OnWritable, typename OnReadable, typename OnTick>
void run_loop(std::vector<pollfd>& fds, double duration_s,
              OnWritable on_writable, OnReadable on_readable, OnTick on_tick) {
    auto start = Clock::now();
    while (!fds.empty() &&
           (duration_s <= 0 || std::chrono::duration<double>(Clock::now() - start).count() < duration_s)) {
        poll(fds.data(), fds.size(), 500);
        for (auto& pfd : fds) {
            if (pfd.fd < 0) continue;
            if (pfd.revents & (POLLHUP | POLLERR)) {
                close(pfd.fd);
                pfd.fd = -1;
                continue;
            }
            if (pfd.revents & POLLOUT) on_writable(pfd);
            if (pfd.fd >= 0 && (pfd.revents & POLLIN)) on_readable(pfd);
        }
        fds.erase(std::remove_if(fds.begin(), fds.end(), [](const pollfd& p) { return p.fd == -1; }), fds.end());
        on_tick();
    }
}

}  // namespace c10k
