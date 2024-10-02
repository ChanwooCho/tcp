#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>  // For atoi() and malloc()
#include <vector>
#include <algorithm> // For std::max
#include <sys/time.h>

unsigned long timeUs() {
    struct timeval te; 
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: server <data_size(KB)> <# of decoders> <# of clients> <port>" << std::endl;
        return -1;
    }

    // Extract command-line arguments
    int data_size = atoi(argv[1]) * 1024; // Convert the data size argument to an integer
    int iterations = atoi(argv[2]) * 2;   // Convert the iterations argument to an integer
    int num_clients = atoi(argv[3]);      // Number of clients to wait for
    int port = atoi(argv[4]);             // Convert the port argument to an integer

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    fd_set read_fds, write_fds;
    std::vector<int> client_sockets;

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
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }

    // Listen for incoming connections
    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }

    std::cout << "Waiting for connections on port " << port << "..." << std::endl;

    // Wait for the specified number of clients to connect
    while (client_sockets.size() < num_clients) {
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_sd = server_fd;

        // Use select() to wait for a new client connection
        int activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);

        if (activity < 0 && errno != EINTR) {
            std::cerr << "Select error" << std::endl;
            break;
        }

        // Check if there’s a new connection request
        if (FD_ISSET(server_fd, &read_fds)) {
            if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
                std::cerr << "Accept failed" << std::endl;
                close(server_fd);
                delete[] buffer;
                delete[] data;
                return -1;
            }

            std::cout << "New client connected." << std::endl;
            client_sockets.push_back(new_socket); // Add new socket to the list
        }

        std::cout << "Waiting for " << num_clients << " clients. Currently connected clients: " << client_sockets.size() << std::endl;
    }

    std::cout << "Minimum " << num_clients << " clients connected. Starting main loop." << std::endl;

    unsigned int before;
    unsigned int interval;

    // Main loop to handle reading and writing for all clients
    for (int e = 0; e < 50; ++e) { // Iterate multiple times as per the original logic
        before = timeUs();
        for (int i = 0; i < iterations; ++i) {
            // After reading from all clients, send data back to all clients
            for (int client_socket : client_sockets) {
                send(client_socket, data, data_size, 0);
            }

            // Read from all connected clients
            for (int client_socket : client_sockets) {
                read(client_socket, buffer, data_size);
            }
        }
        interval = timeUs() - before;
        printf("Averaged Time = %d ms\n", interval / 1000 / e);
    }

    
    sleep(1);
   
    sleep(1);

    // Clean up resources
    for (int client_socket : client_sockets) {
        close(client_socket);
    }
    close(server_fd);
    delete[] buffer;
    delete[] data;

    std::cout << "Connection closed" << std::endl;

    return 0;
}
