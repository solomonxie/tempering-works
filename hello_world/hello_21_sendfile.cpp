/*
    Linux only — compile & run on the EC2 box:
    $ clang++ -std=c++20 -Wall -Wextra -g hello_world/hello_21_sendfile.cpp -o /tmp/hello_21_sendfile && /tmp/hello_21_sendfile
    then:
    curl -s -I -H "Connection: keep-alive" http://localhost:8080 http://localhost:8080
    curl -s -I -H "Connection: close" http://localhost:8080 http://localhost:8080

    Step 13: sendfile() replaces "read the whole file into a std::string, then
    send() that string" from every step since hello_05. That path copies the
    file's bytes from the kernel's page cache into a userspace buffer (the
    ifstream read), then copies them right back into the kernel for the socket
    send — two copies neither of which any application code ever needed to look
    at. sendfile(out_fd, in_fd, &offset, count) tells the kernel to move bytes
    straight from one fd to the other, skipping the userspace round trip.

    Headers are still text we build ourselves, so they still go through send()
    (via the existing PendingWrite path). Only the body — the file's bytes —
    goes through sendfile(), tracked separately as PendingFile so a send that
    can't finish in one syscall resumes on the next writable wakeup, same as
    PendingWrite already does for headers.
*/

#define _GNU_SOURCE  // glibc hides accept4()/sendfile()/SOCK_NONBLOCK behind this with -std=c++20

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>  // fcntl(), O_NONBLOCK, open(), O_RDONLY
#include <cerrno>  // errno, EAGAIN, EWOULDBLOCK
#include <string>
#include <sstream>
#include <filesystem>  // fs:exists()
#include <sys/stat.h>  // fstat()
#include <sys/sendfile.h>  // sendfile()
#include <sys/epoll.h>  // epoll_create1(), epoll_ctl(), epoll_wait(), EPOLLET
#include <vector>
#include <unordered_map>


struct HttpRequest {
    std::string method;
    std::string path;
    std::string http_version;
    bool keep_alive = false;
};

// No more `body` string — the body lives in an open file fd, sent via sendfile().
struct HttpResponse {
    int status_code = 200;
    std::string status_name = "OK";
    std::string mimetype = "text/plain";
    std::string header;
    int file_fd = -1;
    off_t file_size = 0;
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

    if (!std::filesystem::exists(filename)) {
        response.status_code = 404;
        response.status_name = "NOT EXIST";
        filename = "hello_world/static/404.html";
    }
    response.mimetype = guess_mimetype(filename);

    response.file_fd = open(filename.c_str(), O_RDONLY);
    struct stat file_stat{};
    fstat(response.file_fd, &file_stat);
    response.file_size = file_stat.st_size;

    response.header =
        "HTTP/1.1 "+ std::to_string(response.status_code) +" "+ response.status_name +" \r\n"
        "Content-Type: "+ response.mimetype +"\r\n"
        "Content-Length: "+ std::to_string(response.file_size) +"\r\n"
        "Connection: "+ std::string(request.keep_alive ? "keep-alive" : "close") +"\r\n"
        "\r\n";
    return response;
}


// Header text, plus the file waiting behind it once the header itself is done.
struct PendingWrite {
    std::string data;
    int file_fd;
    off_t file_size;
    bool keep_alive;
};

// A sendfile() in progress: how far into the file we are, how much is left.
struct PendingFile {
    int fd;
    off_t offset;
    off_t remaining;
    bool keep_alive;
};


bool try_sendfile(int epfd, int client_fd, PendingFile file,
                   std::unordered_map<int, PendingFile>& pending_files) {
    while (file.remaining > 0) {
        ssize_t sent = sendfile(client_fd, file.fd, &file.offset, file.remaining);
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pending_files[client_fd] = file;
            epoll_event ev{};
            ev.data.fd = client_fd;
            ev.events = EPOLLOUT | EPOLLET;
            epoll_ctl(epfd, EPOLL_CTL_MOD, client_fd, &ev);
            return true;
        } else if (sent <= 0) {
            close(file.fd);
            pending_files.erase(client_fd);
            return false;
        }
        file.remaining -= sent;
    }
    close(file.fd);
    pending_files.erase(client_fd);
    epoll_event ev{};
    ev.data.fd = client_fd;
    ev.events = EPOLLIN | EPOLLET;
    epoll_ctl(epfd, EPOLL_CTL_MOD, client_fd, &ev);
    return file.keep_alive;
}


bool try_send(int epfd, int client_fd, PendingWrite write,
              std::unordered_map<int, PendingWrite>& pending_writes,
              std::unordered_map<int, PendingFile>& pending_files) {
    int bytes_sent = send(client_fd, write.data.c_str(), write.data.size(), 0);
    if (bytes_sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        bytes_sent = 0;
    } else if (bytes_sent < 0) {
        close(write.file_fd);
        pending_writes.erase(client_fd);
        return false;
    }
    if (static_cast<size_t>(bytes_sent) < write.data.size()) {
        write.data.erase(0, bytes_sent);
        pending_writes[client_fd] = write;
        epoll_event ev{};
        ev.data.fd = client_fd;
        ev.events = EPOLLOUT | EPOLLET;
        epoll_ctl(epfd, EPOLL_CTL_MOD, client_fd, &ev);
        return true;
    }
    pending_writes.erase(client_fd);
    // Header fully flushed — hand off to sendfile() for the body.
    PendingFile file{write.file_fd, 0, write.file_size, write.keep_alive};
    return try_sendfile(epfd, client_fd, file, pending_files);
}


bool drain_recv(int client_fd, std::string& raw_request) {
    char buffer[1024];
    while (true) {
        int bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes_received > 0) {
            raw_request.append(buffer, bytes_received);
            continue;
        }
        if (bytes_received == 0) {
            return false;
        }
        return errno == EAGAIN || errno == EWOULDBLOCK;
    }
}


bool handle_client(int epfd, int client_fd, std::unordered_map<int, PendingWrite>& pending_writes,
                    std::unordered_map<int, PendingFile>& pending_files,
                    std::unordered_map<int, std::string>& pending_reads) {
    auto pending_file = pending_files.find(client_fd);
    if (pending_file != pending_files.end()) {
        return try_sendfile(epfd, client_fd, pending_file->second, pending_files);
    }

    auto pending_write = pending_writes.find(client_fd);
    if (pending_write != pending_writes.end()) {
        return try_send(epfd, client_fd, pending_write->second, pending_writes, pending_files);
    }

    std::string& raw_request = pending_reads[client_fd];
    if (!drain_recv(client_fd, raw_request)) {
        pending_reads.erase(client_fd);
        return false;
    }

    if (raw_request.find("\r\n\r\n") == std::string::npos) {
        return true;
    }

    HttpRequest request = parse_request(raw_request);
    HttpResponse response = compose_repsonse(request);
    pending_reads.erase(client_fd);
    PendingWrite write{response.header, response.file_fd, response.file_size, request.keep_alive};
    return try_send(epfd, client_fd, write, pending_writes, pending_files);
}


void cleanup_client(int epfd, int client_fd, std::unordered_map<int, PendingWrite>& pending_writes,
                     std::unordered_map<int, PendingFile>& pending_files,
                     std::unordered_map<int, std::string>& pending_reads) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    auto pending_file = pending_files.find(client_fd);
    if (pending_file != pending_files.end()) {
        close(pending_file->second.fd);
        pending_files.erase(pending_file);
    }
    auto pending_write = pending_writes.find(client_fd);
    if (pending_write != pending_writes.end()) {
        close(pending_write->second.file_fd);
        pending_writes.erase(pending_write);
    }
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
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    int epfd = epoll_create1(0);
    epoll_event server_ev{};
    server_ev.events = EPOLLIN | EPOLLET;
    server_ev.data.fd = server_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &server_ev);

    std::vector<epoll_event> events(64);
    std::unordered_map<int, PendingWrite> pending_writes;
    std::unordered_map<int, PendingFile> pending_files;
    std::unordered_map<int, std::string> pending_reads;

    while (true) {
        std::cout << "Waiting for a client...\n";
        int n = epoll_wait(epfd, events.data(), events.size(), -1);

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            uint32_t revents = events[i].events;

            if (fd != server_fd && (revents & (EPOLLHUP | EPOLLERR))) {
                std::cout << "Connection with client " << fd << " closed (HUP/ERR).\n";
                cleanup_client(epfd, fd, pending_writes, pending_files, pending_reads);
                continue;
            }
            if (fd == server_fd) {
                while (true) {
                    int client_fd = accept4(server_fd, nullptr, nullptr, SOCK_NONBLOCK);
                    if (client_fd < 0) {
                        break;
                    }
                    std::cout << "Accepted client FD at: " << client_fd << std::endl;
                    epoll_event client_ev{};
                    client_ev.events = EPOLLIN | EPOLLET;
                    client_ev.data.fd = client_fd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &client_ev);
                }
            } else {
                bool keep_alive = handle_client(epfd, fd, pending_writes, pending_files, pending_reads);
                if (!keep_alive) {
                    std::cout << "Connection with client " << fd << " closed.\n";
                    cleanup_client(epfd, fd, pending_writes, pending_files, pending_reads);
                }
            }
        }
    }

    return 0;
}
