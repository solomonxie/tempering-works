/*
    $ clang++ -std=c++20 -Wall -Wextra -g hello_world/hello_9_nonblocking.cpp -o /tmp/hello_9_nonblocking && /tmp/hello_9_nonblocking
    then:
    curl -s -I -H "Connection: keep-alive" http://localhost:8080 http://localhost:8080
    curl -s -I -H "Connection: close" http://localhost:8080 http://localhost:8080

    No more std::thread: a single thread now serves every client. select() tells us
    which fds have data waiting so we never call recv()/accept() and block on one
    slow/idle client while others wait.

The goal of this phase is to change our server from:

thread
  ↓
accept()
  ↓
recv()   ← may BLOCK
  ↓
send()   ← may BLOCK
  ↓
recv()   ← may BLOCK
  ↓
...

into an event-driven server:
                 ┌───────────────┐
                 │   event loop  │
                 └───────┬───────┘
                         │
              ┌──────────┼──────────┐
              ↓          ↓          ↓
           socket A   socket B   socket C
              │          │          │
           readable   readable   writable
              │          │          │
              └──────────┼──────────┘
                         ↓
                    do some work
                         ↓
                    back to loop

The important idea is:
Never wait for one client. Check which sockets are ready, work on them, then move on.
*/

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>  // close()
#include <cerrno>
#include <string>
#include <sstream>
#include <fstream>  // std::ifstream
#include <filesystem>
#include <sys/select.h>  // select(), fd_set
#include <fcntl.h>  // changing socket behavior(flag). fcntl=f-cntl=file control
#include <poll.h>  // poll(), pollfd, monitoring multiple sockets


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


void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int create_server() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // Change behavior(flag) of server socket: make it non-blocking
    // flasg | O_NONBLOCK ==> means
    int flags = fcntl(server_fd, F_GETFL, 0);  // F_GETFL = File Get Flags, get current flags
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK); // F_SETFL = File Set Flags, change to non-blocking

    int bind_success = bind(
        server_fd,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address)
    );
    std::cout << "Binded socket succesfully: " << bind_success << std::endl;

    int listen_success = listen(server_fd, 10);
    std::cout << "Listened succesfully: " << listen_success << std::endl;

    return server_fd;
}


// Handles one ready-to-read event on client_fd. Returns false if the fd should
// be closed and dropped (client hung up, real error, or "Connection: close").
bool handle_client(int client_fd) {
    char buffer[1024];
    int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received < 0) {
        // select() said this fd was readable, so EAGAIN here would be spurious;
        // treat it as "nothing to do yet" rather than closing the connection.
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        return false;
    }
    if (bytes_received == 0) {
        return false;  // client closed its end
    }
    std::string raw_request(buffer, bytes_received);

    HttpRequest request = parse_request(raw_request);
    HttpResponse response = compose_repsonse(request);

    // Non-blocking send() can also return EAGAIN on a full socket buffer;
    // ignored here for simplicity, handled properly once buffering arrives (Phase 12).
    send(client_fd, response.body.c_str(), response.body.size(), 0);

    return request.keep_alive;
}


int main() {
    int server_fd = create_server();

    // The set of fds we ask select() to watch for readability: the listening
    // socket itself (new connections) plus every currently open client socket.
    fd_set master_fds;
    FD_ZERO(&master_fds);
    FD_SET(server_fd, &master_fds);
    int max_fd = server_fd;

    while (true) {
        fd_set read_fds = master_fds;  // select() mutates its set, so pass a copy
        select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);

        for (int fd = 0; fd <= max_fd; fd++) {
            if (!FD_ISSET(fd, &read_fds)) {
                continue;
            }

            if (fd == server_fd) {
                // Drain every pending connection now, since select() only wakes us
                // once even if several clients connected in the meantime.
                int client_fd;
                while ((client_fd = accept(server_fd, nullptr, nullptr)) > 0) {
                    std::cout << "Accepted client FD at: " << client_fd << std::endl;
                    int flags = fcntl(client_fd, F_GETFL, 0);
                    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
                    FD_SET(client_fd, &master_fds);
                    if (client_fd > max_fd) {
                        max_fd = client_fd;
                    }
                }
                continue;
            }

            bool keep_open = handle_client(fd);
            if (!keep_open) {
                std::cout << "Connection with client " << fd << " closed.\n";
                close(fd);
                FD_CLR(fd, &master_fds);
            }
        }
    }

    return 0;
}
