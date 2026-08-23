/*
    $ clang++ -std=c++20 -Wall -Wextra -g hello_world/hello_6_file_links.cpp -o /tmp/hello_6_file_links && /tmp/hello_6_file_links
    then:
    curl -v localhost:8080/about
    or:
    open from browser: http://localhost:8080/about
*/

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <sstream>
#include <fstream>  // std::ifstream
#include <filesystem>  // fs:exists()


struct HttpRequest {
    std::string method;
    std::string path;
    std::string http_version;
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
    return request;
}


HttpResponse compose_repsonse(std::string path) {
    HttpResponse response;
    // Route requst
    if (path == "/") {
        path = "/index.html";
    }
    std::string filename = "hello_world/static" + path;
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
        "\r\n"
        + content;
    return response;
}


int handle_client(int client_fd) {
    char buffer[1024];
    int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    std::string raw_request(buffer, bytes_received);

    HttpRequest request = parse_request(raw_request);
    HttpResponse response = compose_repsonse(request.path);

    int bytes_sent = send(client_fd, response.body.c_str(), response.body.size(), 0);
    close(client_fd);
    return bytes_sent;
}


int main() {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    int bind_success = bind(
            server_fd,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)
            );
    std::cout << "Binded socket succesfully: " << bind_success << std::endl;

    int listen_success = listen(server_fd, 10);
    std::cout << "Listened succesfully: " << listen_success << std::endl;

    while (true) {
        std::cout << "Waiting for a client...\n";
        int client_fd = accept(server_fd, nullptr, nullptr);
        std::cout << "Accepted client FD at: " << client_fd << std::endl;

        handle_client(client_fd);
    }

    return 0;
}
