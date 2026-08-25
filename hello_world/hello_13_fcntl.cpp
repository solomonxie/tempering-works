/*
    $ clang++ -std=c++20 -Wall -Wextra -g hello_world/hello_13_fcntl.cpp -o /tmp/hello_13_fcntl && /tmp/hello_13_fcntl
    then:
    curl -s -I -H "Connection: keep-alive" http://localhost:8080 http://localhost:8080
    curl -s -I -H "Connection: close" http://localhost:8080 http://localhost:8080

    No more std::thread: a single thread now serves every client. poll() tells us
    which fds have data waiting so we never call recv()/accept() and block on one
    slow/idle client while others wait.

    Step 5: client fds are set O_NONBLOCK and handled one request at a time —
    handle_client() no longer loops until the keep-alive connection closes, so a
    single client can't hog the loop; it's revisited on the next poll() readiness.
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
    std::istringstream stream(raw_request);  // e..g, "GET /hello HTTP/1.1"
    HttpRequest request;
    stream >> request.method;  // parse whitespace seperated parts, stream like cout<<
    stream >> request.path;
    stream >> request.http_version;
    // HTTP/1.1 defaults to persistent connections, HTTP/1.0 defaults to close
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
    // Route requst
    if (request.path == "/") {
        request.path = "/index.html";
    }
    std::string filename = "hello_world/static" + request.path;
    std::cout<< "Reading file path: " + filename << std::endl;

    // Check existence
    if (!std::filesystem::exists(filename)) {
        response.status_code = 404;
        response.status_name = "NOT EXIST";
        filename = "hello_world/static/404.html";
    }

    // Check Mime Type
    response.mimetype = guess_mimetype(filename);

    // Read file for response
    std::cout<< "Reading content from file: " << filename << std::endl;
    std::ifstream myfile(filename);
    std::string content((std::istreambuf_iterator<char>(myfile)), std::istreambuf_iterator<char>());

    // Wrap with HTTP Response
    response.body =
        "HTTP/1.1 "+ std::to_string(response.status_code) +" "+ response.status_name +" \r\n"
        "Content-Type: "+ response.mimetype +"\r\n"
        "Content-Length: "+ std::to_string(content.size()) +"\r\n"
        "Connection: "+ std::string(request.keep_alive ? "keep-alive" : "close") +"\r\n"
        "\r\n"
        + content;
    return response;
}


// Handles exactly one request off client_fd, then returns whether to keep polling it.
// No inner loop: with client_fd non-blocking, looping here would just spin on
// EAGAIN once the client goes idle instead of yielding back to poll().
bool handle_client(int client_fd) {
    char buffer[1024];

    int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        // EAGAIN/EWOULDBLOCK: poll() said readable but nothing to read yet (or a
        // spurious wakeup) — keep the connection open and try again next time.
        if (bytes_received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        close(client_fd);
        std::cout << "Connection with client " << client_fd << " closed.\n";
        return false;
    }
    std::string raw_request(buffer, bytes_received);

    HttpRequest request = parse_request(raw_request);
    HttpResponse response = compose_repsonse(request);

    int bytes_sent = send(client_fd, response.body.c_str(), response.body.size(), 0);
    if (bytes_sent <= 0 || !request.keep_alive) {
        close(client_fd);
        std::cout << "Connection with client " << client_fd << " closed.\n";
        return false;
    }
    std::cout << "Keeping connection alive on FD: " << client_fd << std::endl;
    return true;
}


int main() {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    int reuse = 1;  // 0: disable; 1: enable
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

    while (true) {
        std::cout << "Waiting for a client...\n";
        poll(fds.data(), fds.size(), -1);

        for (size_t i = 0; i < fds.size(); i++) {
            if (!(fds[i].revents & POLLIN)) {
                continue;
            }
            if (fds[i].fd == server_fd) {
                // no need to fcntl(server_fd),
                // because when POLLIN happens, accept() is guaranteed to return immediately
                int client_fd = accept(server_fd, nullptr, nullptr);
                // Step 5: never block on this client's recv/send
                fcntl(client_fd, F_SETFL, O_NONBLOCK);
                std::cout << "Accepted client FD at: " << client_fd << std::endl;
                fds.push_back({client_fd, POLLIN, 0});
            } else {
                // should not delete socket after handling, unless it asks to close
                bool keep_alive = handle_client(fds[i].fd);
                if (!keep_alive) {
                    fds.erase(fds.begin() + i);
                    i--;
                }
            }
        }
    }

    return 0;
}
