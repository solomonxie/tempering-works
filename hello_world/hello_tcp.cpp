/*
    $ clang++ -std=c++20 -Wall -Wextra -g hello_tcp.cpp -o build/hello_tcp && ./build/hello_tcp
    then:
    $ telnet localhost 8080
    then: type anything then hit enter.
*/

#include <iostream>  // console I/O
#include <sys/socket.h>  // OS Socket lib: socket, listen, bind, send...
#include <netinet/in.h>  // internet lib: sockaddr_in
#include <unistd.h>  // POSIX standard lib: close()
#include <string>


int main() {
    std::cout<< "hello, tcp!\n";

    // Target binding address:
    sockaddr_in address{};  // initialize struct with 0
    address.sin_family = AF_INET;  // IPv4. sin=(s)ocket (in)ternet, AF_INET=(A)ddress (F)amily — (IN)ternet
    address.sin_addr.s_addr = INADDR_ANY;  // (in)ternet (addr)res: any local addresses: 127.0.0.1, 192.168.1.1 ...
    address.sin_port = htons(8080);  // (h)ost-(to)-(n)etwork-(s)hort

    // (IPv4, stream_oriented, 0: choose protocol based on ipv4+stream = TCP)
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    std::cout << "Created server FD at: " << server_fd << std::endl;

    // bind: "Associate this socket with this local network address"
    int bind_success = bind(
        server_fd,
        // reinterpret_cast: convert pointer type to another pointer type
        // in this case: sockaddr_in --> sockaddr, because bind accept sockaddr type
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address)
    );
    std::cout << "Binded socket succesfully: " << bind_success << std::endl;

    int listen_success = listen(server_fd, 10);
    std::cout << "Listened succesfully: " << listen_success << std::endl;

    // while (true) {  // don't do loop yet, just accept one client
    std::cout << "Waiting for a client...\n";
    int client_fd = accept(server_fd, nullptr, nullptr);
    std::cout << "Accepted client FD at: " << client_fd << std::endl;

    /*
       buffer
       ┌───┬───┬───┬───┬───┬───┬─── ... ───┐
       │   │   │   │   │   │   │           │
       └───┴───┴───┴───┴───┴───┴─── ... ───┘
        1024 bytes
    */
    char buffer[1024];  // a fixed buffer to accept user request/input
    int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    std::string received_string(buffer, bytes_received);  // request_sting(...) way to construct a str var "request_sting".
    std::cout<< "Received: " << received_string << std::endl;

    std::string msg = "Hi, client!, Did you say "+ received_string +" ?\n";
    int bytes_sent = send(client_fd, msg.c_str(), msg.size(), 0);
    std::cout << "Sent bytes of: " << bytes_sent << std::endl;

    int close_success = close(client_fd);
    std::cout << "Closed client succesfully: " << close_success << std::endl;

    return 0;
}
