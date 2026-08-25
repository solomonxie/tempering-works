/*
    $ clang++ -std=c++20 -Wall -Wextra -g hello_world/hello_9_nonblocking.cpp -o /tmp/hello_9_nonblocking && /tmp/hello_9_nonblocking
    then:
    curl -s -I -H "Connection: keep-alive" http://localhost:8080 http://localhost:8080
    curl -s -I -H "Connection: close" http://localhost:8080 http://localhost:8080

    No more std::thread: a single thread now serves every client. poll() tells us
    which fds have data waiting so we never call recv()/accept() and block on one
    slow/idle client while others wait.
*/

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>  // fcntl(), O_NONBLOCK
#include <cerrno>  // errno, EAGAIN, EWOULDBLOCK
#include <string>
#include <sstream>
#include <fstream>  // std::ifstream
#include <filesystem>  // fs:exists()
#include <poll.h>  // poll(), pollfd, POLLIN, socket event monitoring
#include <vector>
#include <unordered_map>


struct HttpRequest {
    std::string method;
    std::string path;
    std::string http_version;
    bool keep_alive = false;
};

struct HttpResponse {
    int status_code = 200;
    std::string status_name = "OK";
    std::string mimetype = "text/plain";
    std::string body;
};

std::string guess_mimetype(std::string filename) {
    std::string mimetype;
    if (filename.ends_with(".html")) {
        mimetype = "text/html";
    } else if (filename.ends_with(".css")) {
        mimetype = "text/css";
    } else if (filename.ends_with(".js")) {
        mimetype = "application/javascript";
    } else if (filename.ends_with(".json")) {
        mimetype = "application/json";
    } else if (filename.ends_with(".jpeg")) {
        mimetype = "image/jpeg";
    } else if (filename.ends_with(".png")) {
        mimetype = "image/png";
    } else {
        mimetype = "text/plain";
    }
    return mimetype;
}


HttpRequest parse_request(std::string raw_request) {
    std::istringstream stream(raw_request);
    HttpRequest request;
    stream >> request.method;
    stream >> request.path;
    stream >> request.http_version;
    if (request.http_version == "HTTP/1.1") {
        request.keep_alive = true;
    } else if (raw_request.find("Connection: keep-alive") != std::string::npos) {
        request.keep_alive = true;
    }
    if (raw_request.find("Connection: close") != std::string::npos) {
        request.keep_alive = false;
    }
    return request;
}


HttpResponse compose_repsonse(HttpRequest request) {
    HttpResponse response;
    if (request.path == "/") {
        request.path = "/index.html";
    }
    std::string filename = "hello_world/static" + request.path;
    std::cout<< "Reading file path: " + filename << std::endl;

    if (!std::filesystem::exists(filename)) {
        response.status_code = 404;
        response.status_name = "NOT EXIST";
        filename = "hello_world/static/404.html";
    }

    response.mimetype = guess_mimetype(filename);

    std::cout<< "Reading content from file: " << filename << std::endl;
    std::ifstream myfile(filename);
    std::string content((std::istreambuf_iterator<char>(myfile)), std::istreambuf_iterator<char>());

    response.body =
        "HTTP/1.1 "+ std::to_string(response.status_code) +" "+ response.status_name +" \r\n"
        "Content-Type: "+ response.mimetype +"\r\n"
        "Content-Length: "+ std::to_string(content.size()) +"\r\n"
        "Connection: "+ std::string(request.keep_alive ? "keep-alive" : "close") +"\r\n"
        "\r\n"
        + content;
    return response;
}


// Step 8: what's left of a response that didn't fully fit in one send(), plus
// whether to close the connection once the rest has gone out.
struct PendingWrite {
    std::string data;
    bool keep_alive;
};


bool try_send(int client_fd, const std::string& data, bool keep_alive, pollfd& pfd,
              std::unordered_map<int, PendingWrite>& pending_writes) {
    int bytes_sent = send(client_fd, data.c_str(), data.size(), 0);
    if (bytes_sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        bytes_sent = 0;
    } else if (bytes_sent < 0) {
        return false;
    }
    // Detected data was sent partially:
    if (static_cast<size_t>(bytes_sent) < data.size()) {
        pending_writes[client_fd] = {data.substr(bytes_sent), keep_alive};  // .substr(pos) returns everything after pos
        // why override target event here instead of registering both POLLIN|POLLOUT?
        // because client is almost always write ready, but it's a waste if nothing is to write
        pfd.events = POLLOUT;
        return true;
    }
    pending_writes.erase(client_fd);
    pfd.events = POLLIN;
    return keep_alive;
}


// handle_client() do EITHER: send() or recv()
// basic workflow:
// 1. send pending data
// 2. receive data from client, only if no partial data to send
// 3. send final response to client
// Why "either" not both? because `pfd.events` can only register 1 event?
bool handle_client(pollfd& pfd, std::unordered_map<int, PendingWrite>& pending_writes) {
    int client_fd = pfd.fd;

    // Send to client:
    // Step8: Detected if we have pending partial data yet to send to client
    auto pending = pending_writes.find(client_fd);
    if (pending != pending_writes.end()) {
        PendingWrite write = pending->second;
        // send the partial data, might also be sent partially (1/2 data, we may send 1/4)
        return try_send(client_fd, write.data, write.keep_alive, pfd, pending_writes);
    }

    // Receive from client (only if there's no pending data to send first)
    char buffer[1024];
    int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return true;
    } else if (bytes_received <= 0) {
        return false;
    }
    std::string raw_request(buffer, bytes_received);

    HttpRequest request = parse_request(raw_request);
    HttpResponse response = compose_repsonse(request);
    return try_send(client_fd, response.body, request.keep_alive, pfd, pending_writes);
}


int main() {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    int bind_success = bind(
            server_fd,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)
            );
    std::cout << "Binded socket succesfully: " << bind_success << std::endl;

    int listen_success = listen(server_fd, 10);
    std::cout << "Listened succesfully: " << listen_success << std::endl;

    std::vector<pollfd> fds;
    fds.push_back({server_fd, POLLIN, 0});

    std::unordered_map<int, PendingWrite> pending_writes;

    while (true) {
        std::cout << "Waiting for a client...\n";
        poll(fds.data(), fds.size(), -1);

        for (size_t i = 0; i < fds.size(); i++) {
            if (fds[i].fd != server_fd && (fds[i].revents & (POLLHUP | POLLERR))) {
                close(fds[i].fd);
                std::cout << "Connection with client " << fds[i].fd << " closed (HUP/ERR).\n";
                pending_writes.erase(fds[i].fd);  // no need to keep closed connection data
                // vector.erase accepts iterator, and .begin() = iterator,
                // iterator+n means "offset by n"
                // fds.begin()+i == fds[i]
                fds.erase(fds.begin() + i);
                i--;
                continue;
            }
            // We only deal with POLLIN / POLLOUT events now (readiness of read/write)
            if (!(fds[i].revents & (POLLIN | POLLOUT))) {
                continue;
            }
            if (fds[i].fd == server_fd) {
                int client_fd = accept(server_fd, nullptr, nullptr);
                if (client_fd < 0) {
                    continue;
                }
                fcntl(client_fd, F_SETFL, O_NONBLOCK);
                std::cout << "Accepted client FD at: " << client_fd << std::endl;
                fds.push_back({client_fd, POLLIN, 0});
            } else {
                bool keep_alive = handle_client(fds[i], pending_writes);
                if (!keep_alive) {
                    close(fds[i].fd);
                    std::cout << "Connection with client " << fds[i].fd << " closed.\n";
                    pending_writes.erase(fds[i].fd);
                    fds.erase(fds.begin() + i);
                    i--;
                }
            }
        }
    }

    return 0;
}
