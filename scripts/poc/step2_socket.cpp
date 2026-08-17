
#include <iostream>
#include <sys/socket.h>


int main() {
    std::cout << "Hello, World!" << std::endl;

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);

    std::cout << "Socket file descriptor: " << sockfd << std::endl;

    return 0;
}
