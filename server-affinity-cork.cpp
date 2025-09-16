#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>  // For atoi() and malloc()
#include <vector>
#include <algorithm> // For std::max
#include <sys/time.h>
#include <thread>

unsigned long timeUs() {
    struct timeval te; 
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

ssize_t read_all(int sock, char* buffer, size_t size) {
    size_t total_read = 0;
    
    while (total_read < size) {
        ssize_t bytes_read = read(sock, buffer + total_read, size - total_read);
        if (bytes_read < 0) {
            perror("Read error");
            return -1;
        } else if (bytes_read == 0) {
            // Connection closed
            break;
        }
        total_read += bytes_read;
    }
    return total_read;
}

ssize_t send_all(int sock, const char* data, size_t size, int c) {
    const size_t CHUNK = c;
    size_t total_send = 0; 
    while (total_send < size) {
        size_t n = size - total_send;
        if (n > CHUNK) n = CHUNK;
        int flags = (total_send + n < size) ? MSG_MORE : 0;
        ssize_t s = send(sock, data + total_send, n, flags);
        if (s < 0) return -1;
        total_send += s;
    }
    return total_send;
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: server <core index(0-7)> <data_size(Bytes)> <# of decoders> <chunck_size(Bytes)> <# of clients> <port>" << std::endl;
        return -1;
    }

    // Core Affinity
    int core_index = std::atoi(argv[1]);
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_index, &cpuset); 
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
        perror("sched_setaffinity");
        return -1;
    }

    // Extract command-line arguments
    int data_size = atoi(argv[2]); 
    int iterations = atoi(argv[3]) * 2;   
    int chunck_size = std::atoi(argv[4]);
    int num_clients = atoi(argv[5]);     
    int port = atoi(argv[6]);            

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
    unsigned int sum_interval = 0;

    
    // Main loop to handle reading and writing for all clients
    for (int e = 0; e < 50; ++e) { // Iterate multiple times as per the original logic
        for (int i = 0; i < iterations; ++i) {
            memset(data, 'A' + i % 26, data_size);
            before = timeUs();
            for (int client_socket : client_sockets) {
                size_t bytes_send = send_all(client_socket, data, data_size, chunck_size);
            }
            for (int client_socket : client_sockets) {
                size_t bytes_read = read_all(client_socket, buffer, data_size);
            }
            interval = timeUs() - before;
            sum_interval += interval;
        }
        printf("iteration %d' Time = %d ms\n\n", e, sum_interval / 1000);
    }

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
