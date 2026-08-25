/*
    $ clang++ -std=c++20 -Wall -Wextra -g c10k-challenge/tests/conn_flood.cpp -o /tmp/conn_flood
    $ /tmp/conn_flood --host 127.0.0.1 --port 8080 --max 10000

    Ramps concurrent *idle* connections in steps, holding each one open
    without sending data, to find how many simultaneous connections the
    server can accept and hold before it starts refusing/erroring.
    --step sets a fixed linear increment; omit it for a doubling ramp
    (100, 200, 400, ... --max).
*/

#include "common.hpp"

using namespace c10k;

std::vector<int> build_steps(int max, int step) {
    std::vector<int> steps;
    if (step > 0) {
        for (int s = step; s < max; s += step) steps.push_back(s);
    } else {
        for (int s = 100; s < max; s *= 2) steps.push_back(s);
    }
    steps.push_back(max);
    return steps;
}

int main(int argc, char** argv) {
    Args args(argc, argv);
    std::string host = args.get("host", "127.0.0.1");
    int port = args.get_int("port", 8080);
    int max_conns = args.get_int("max", 10000);
    int step = args.get_int("step", 0);

    raise_fd_limit();

    sockaddr_in addr;
    if (!resolve(host, port, addr)) {
        std::cerr << "could not resolve " << host << "\n";
        return 1;
    }

    std::vector<pollfd> fds;
    fds.reserve(max_conns);

    for (int target : build_steps(max_conns, step)) {
        auto step_start = Clock::now();
        int attempted = 0, succeeded = 0, failed = 0;
        std::unordered_map<std::string, int> error_counts;

        while (static_cast<int>(fds.size()) < target) {
            attempted++;
            int fd = nonblocking_connect(addr);
            if (fd < 0) {
                failed++;
                error_counts[strerror(errno)]++;
                continue;
            }
            fds.push_back({fd, POLLOUT, 0});
        }

        // Give in-flight connects up to 2s total to finish or fail. poll()
        // returns as soon as any single fd is ready, not once all of them
        // are, so keep polling with the remaining budget until none are
        // still pending.
        auto poll_deadline = Clock::now() + std::chrono::milliseconds(2000);
        while (std::any_of(fds.begin(), fds.end(), [](const pollfd& p) { return p.events == POLLOUT; })) {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(poll_deadline - Clock::now()).count();
            if (remaining <= 0) break;
            poll(fds.data(), fds.size(), static_cast<int>(remaining));
            for (auto& pfd : fds) {
                if (pfd.events != POLLOUT) continue;  // already confirmed, this step or an earlier one
                if (pfd.revents & (POLLHUP | POLLERR)) {
                    failed++;
                    error_counts["connect_failed"]++;
                    close(pfd.fd);
                    pfd.fd = -1;
                    pfd.events = 0;
                } else if (pfd.revents & POLLOUT) {
                    succeeded++;
                    pfd.events = 0;  // confirmed open and idle: stop polling for writability
                }
            }
        }
        fds.erase(std::remove_if(fds.begin(), fds.end(), [](const pollfd& p) { return p.fd == -1; }), fds.end());

        std::cout << "step target=" << target
                   << "  open=" << fds.size()
                   << "  attempted=" << attempted
                   << "  succeeded=" << succeeded
                   << "  failed=" << failed
                   << "  elapsed_ms=" << ms_since(step_start) << "\n";
        for (auto& [tag, count] : error_counts) {
            std::cout << "    error[" << tag << "]: " << count << "\n";
        }
    }

    std::cout << "final open connections: " << fds.size() << "\n";
    for (auto& pfd : fds) close(pfd.fd);
    return 0;
}
