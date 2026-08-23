/*
    $ clang++ -std=c++20 -Wall -Wextra -g hello_world/hello_4_route.cpp -o build/hello_4_route && ./build/hello_4_route
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
#include <sstream> // for (s)tring stream


struct HttpRequest {
    std::string method;
    std::string path;
    std::string http_version;
};


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
    std::cout<< "Received: " << raw_request << std::endl;

    // Parse request
    std::istringstream stream(raw_request);  // e..g, "GET /hello HTTP/1.1"
    HttpRequest request;
    stream >> request.method;  // parse whitespace seperated parts, stream like cout<<
    stream >> request.path;
    stream >> request.http_version;

    // Route requst
    int status_code;
    std::string status_name;
    std::string msg;
    if (request.path == "/") {
        status_code = 200;
        status_name = "OK";
        msg = "Welcome to root page.";
    } else if (request.path == "/about") {
        status_code = 200;
        status_name = "OK";
        msg = "This is my page.";
    } else {
        status_code = 404;
        status_name = "NOT FOUND";
        msg = "Page not found.";
    }

    // Make response
    std::string body =
        "<h1>Parsed Request</h1>\n"
        "<p>Method: "+ request.method +"</p>\n"
        "<p>Path: "+ request.path +"</p>\n"
        "<p>HTTP Version: "+ request.http_version +"</p>\n\n"
        "<h3>"+ msg +"</h3>";
    std::string response =
        "HTTP/1.1 "+ std::to_string(status_code) +" "+ status_name +" \r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: "+ std::to_string(body.size()) +"\r\n"
        "\r\n" + body;
    int bytes_sent = send(client_fd, response.c_str(), response.size(), 0);
    std::cout << "Sent bytes of: " << bytes_sent << std::endl;

    close(client_fd);
    close(server_fd);

    return 0;
}
