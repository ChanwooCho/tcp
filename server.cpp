#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>  // For atoi() and malloc()

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: server <data_size(KB)> <# of decoders> <port>" << std::endl;
        return -1;
    }

    // Extract command-line arguments
    int data_size = atoi(argv[1]) * 1024;       // Convert the data size argument to an integer
    int iterations = atoi(argv[2]) * 2;      // Convert the iterations argument to an integer
    int port = atoi(argv[3]);            // Convert the port argument to an integer

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Dynamically allocate buffer and data arrays based on the specified data size
    char* buffer = new char[data_size];
    char* data = new char[data_size];

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "Socket failed" << std::endl;
        delete[] buffer;
        delete[] data;
        return -1;
    }

    // Attach socket to the port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        std::cerr << "setsockopt failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Bind to any local IP address
    address.sin_port = htons(port);       // Use the port passed as an argument

    // Bind the socket to the network address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }

    std::cout << "Waiting for connection on port " << port << "..." << std::endl;

    // Accept incoming connection
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        std::cerr << "Accept failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }

    std::cout << "Connection established with client." << std::endl;

    // Run the specified number of iterations
    for (int e = 0; e < 50; ++e) {
        for (int i = 0; i < iterations; ++i) {
            // Receive data from client
            read(new_socket, buffer, data_size);

            // Send data back to client
            send(new_socket, data, data_size, 0);
        }
    }

    // Clean up resources
    close(new_socket);
    close(server_fd);
    delete[] buffer;
    delete[] data;

    std::cout << "Connection closed" << std::endl;

    return 0;
}
