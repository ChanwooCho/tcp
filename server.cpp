#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>  // For atoi()

#define DATA_SIZE 20480  // 20KB
#define ITERATIONS 160

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: server <port>" << std::endl;
        return -1;
    }

    int port = atoi(argv[1]);  // Convert the port argument to an integer

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[DATA_SIZE] = {0};
    char data[DATA_SIZE] = {0};

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "Socket failed" << std::endl;
        return -1;
    }

    // Attach socket to the port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        std::cerr << "setsockopt failed" << std::endl;
        close(server_fd);
        return -1;
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Bind to any local IP address
    address.sin_port = htons(port);       // Use the port passed as an argument

    // Bind the socket to the network address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        return -1;
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        return -1;
    }

    std::cout << "Waiting for connection on port " << port << "..." << std::endl;

    // Accept incoming connection
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        std::cerr << "Accept failed" << std::endl;
        close(server_fd);
        return -1;
    }

    std::cout << "Connection established with client." << std::endl;
    

    for (int i = 0; i < ITERATIONS; ++i) {

        // Receive 20KB data from client
        read(new_socket, buffer, DATA_SIZE);

        // Send 20KB data back to client
        send(new_socket, data, DATA_SIZE, 0);
    }
    
    // Close sockets
    close(new_socket);
    close(server_fd);
    std::cout << "Connection closed" << std::endl;

    return 0;
}
