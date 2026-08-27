/*
    Linux only — compile & run on the EC2 box:
    $ clang++ -std=c++20 -Wall -Wextra -g hello_world/hello_20_epoll_edge_triggered.cpp -o /tmp/hello_20_epoll_edge_triggered && /tmp/hello_20_epoll_edge_triggered
    then:
    curl -s -I -H "Connection: keep-alive" http://localhost:8080 http://localhost:8080
    curl -s -I -H "Connection: close" http://localhost:8080 http://localhost:8080

    Step 12: EPOLLET (edge-triggered) instead of the level-triggered default from
    hello_19. Level-triggered means epoll_wait() keeps telling you a fd is
    readable for as long as unread bytes sit in its buffer, even across many
    calls — one recv() per wakeup, like hello_19 did, is safe. Edge-triggered
    means you're only told once, at the moment readability *changes* from "no
    data" to "data arrived". Miss that one notification without draining the fd
    all the way to EAGAIN, and the remaining bytes sit there silently — no fd
    limit re-arrives, because nothing changed. So both accept() and recv() are
    now called in a loop until they return EAGAIN, not just once.
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
#include <sys/epoll.h>  // epoll_create1(), epoll_ctl(), epoll_wait(), EPOLLET
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


struct PendingWrite {
    std::string data;
    bool keep_alive;
};


bool try_send(int epfd, int client_fd, const std::string& data, bool keep_alive,
              std::unordered_map<int, PendingWrite>& pending_writes) {
    int bytes_sent = send(client_fd, data.c_str(), data.size(), 0);
    if (bytes_sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        bytes_sent = 0;
    } else if (bytes_sent < 0) {
        return false;
    }
    epoll_event ev{};
    ev.data.fd = client_fd;
    if (static_cast<size_t>(bytes_sent) < data.size()) {
        pending_writes[client_fd] = {data.substr(bytes_sent), keep_alive};
        ev.events = EPOLLOUT | EPOLLET;
        epoll_ctl(epfd, EPOLL_CTL_MOD, client_fd, &ev);
        return true;
    }
    pending_writes.erase(client_fd);
    ev.events = EPOLLIN | EPOLLET;
    epoll_ctl(epfd, EPOLL_CTL_MOD, client_fd, &ev);
    return keep_alive;
}


// Edge-triggered: drain recv() until it returns EAGAIN, or we'd never see this
// fd's remaining bytes again until more new data happens to arrive.
bool drain_recv(int client_fd, std::string& raw_request) {
    char buffer[1024];
    while (true) {
        int bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes_received > 0) {
            raw_request.append(buffer, bytes_received);
            continue;
        }
        if (bytes_received == 0) {
            return false;  // peer closed
        }
        return errno == EAGAIN || errno == EWOULDBLOCK;  // drained vs. real error
    }
}


bool handle_client(int epfd, int client_fd, std::unordered_map<int, PendingWrite>& pending_writes,
                    std::unordered_map<int, std::string>& pending_reads) {
    auto pending_write = pending_writes.find(client_fd);
    if (pending_write != pending_writes.end()) {
        PendingWrite write = pending_write->second;
        return try_send(epfd, client_fd, write.data, write.keep_alive, pending_writes);
    }

    std::string& raw_request = pending_reads[client_fd];
    if (!drain_recv(client_fd, raw_request)) {
        pending_reads.erase(client_fd);
        return false;
    }

    // Still no full header block — more data may already be sitting behind this
    // one, so we've drained it all; go back to epoll_wait() for the next edge.
    if (raw_request.find("\r\n\r\n") == std::string::npos) {
        return true;
    }

    HttpRequest request = parse_request(raw_request);
    HttpResponse response = compose_repsonse(request);
    pending_reads.erase(client_fd);
    return try_send(epfd, client_fd, response.body, request.keep_alive, pending_writes);
}


void cleanup_client(int epfd, int client_fd, std::unordered_map<int, PendingWrite>& pending_writes,
                     std::unordered_map<int, std::string>& pending_reads) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    pending_writes.erase(client_fd);
    pending_reads.erase(client_fd);
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
    fcntl(server_fd, F_SETFL, O_NONBLOCK);  // needed so the accept-drain loop below can hit EAGAIN

    int epfd = epoll_create1(0);
    epoll_event server_ev{};
    server_ev.events = EPOLLIN | EPOLLET;
    server_ev.data.fd = server_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &server_ev);

    std::vector<epoll_event> events(64);
    std::unordered_map<int, PendingWrite> pending_writes;
    std::unordered_map<int, std::string> pending_reads;

    while (true) {
        std::cout << "Waiting for a client...\n";
        int n = epoll_wait(epfd, events.data(), events.size(), -1);

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            uint32_t revents = events[i].events;

            if (fd != server_fd && (revents & (EPOLLHUP | EPOLLERR))) {
                std::cout << "Connection with client " << fd << " closed (HUP/ERR).\n";
                cleanup_client(epfd, fd, pending_writes, pending_reads);
                continue;
            }
            if (fd == server_fd) {
                // Edge-triggered: one wakeup can mean several pending connections,
                // so accept() in a loop until it runs dry, not just once.
                while (true) {
                    int client_fd = accept(server_fd, nullptr, nullptr);
                    if (client_fd < 0) {
                        break;
                    }
                    fcntl(client_fd, F_SETFL, O_NONBLOCK);
                    std::cout << "Accepted client FD at: " << client_fd << std::endl;
                    epoll_event client_ev{};
                    client_ev.events = EPOLLIN | EPOLLET;
                    client_ev.data.fd = client_fd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &client_ev);
                }
            } else {
                bool keep_alive = handle_client(epfd, fd, pending_writes, pending_reads);
                if (!keep_alive) {
                    std::cout << "Connection with client " << fd << " closed.\n";
                    cleanup_client(epfd, fd, pending_writes, pending_reads);
                }
            }
        }
    }

    return 0;
}
