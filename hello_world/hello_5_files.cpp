/*
    $ clang++ -std=c++20 -Wall -Wextra -g hello_world/hello_5_files.cpp -o build/hello_5_files && ./build/hello_5_files
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

// std::<std::unordered_map><int, std::string>


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

    std::cout << "Waiting for a client...\n";
    int client_fd = accept(server_fd, nullptr, nullptr);
    std::cout << "Accepted client FD at: " << client_fd << std::endl;

    char buffer[1024];
    int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    std::string raw_request(buffer, bytes_received);
    // std::cout<< "Received: " << raw_request << std::endl;

    // Parse request
    std::istringstream stream(raw_request);  // e..g, "GET /hello HTTP/1.1"
    HttpRequest request;
    stream >> request.method;  // parse whitespace seperated parts, stream like cout<<
    stream >> request.path;
    stream >> request.http_version;

    // Route requst
    int status_code = 200;
    std::string status_name = "OK";
    if (request.path == "/") {
        request.path = "/index.html";
    }
    std::string filename = "hello_world/static" + request.path;
    std::cout<< "Reading file path: " + filename << std::endl;

    // Check existence
    if (!std::filesystem::exists(filename)) {
        status_code = 404;
        status_name = "NOT EXIST";
        filename = "hello_world/static/404.html";
    }

    // Check Mime Type
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

    // Read file for response
    std::cout<< "Reading content from file: " << filename << std::endl;
    std::ifstream myfile(filename);  // ifstream = (i)nput (f)ile (stream), opens a file for reading.
    // istreambuf_iterator = (i)nput (s)tream (buf)fer
    // is an iterator go through characters from input stream.
    // body(begin, end)
    std::string body(
        (std::istreambuf_iterator<char>(myfile)),
        std::istreambuf_iterator<char>()
    );

    // Wrap with HTTP Response
    std::string response =
        "HTTP/1.1 "+ std::to_string(status_code) +" "+ status_name +" \r\n"
        "Content-Type: "+ mimetype +"\r\n"
        "Content-Length: "+ std::to_string(body.size()) +"\r\n"
        "\r\n" + body;
    send(client_fd, response.c_str(), response.size(), 0);
    // std::cout << "Sent bytes of: " << bytes_sent << std::endl;

    close(client_fd);
    close(server_fd);

    return 0;
}
