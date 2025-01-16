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

ssize_t read_all(int sock, char* buffer, size_t size, int e, int d) {
    size_t total_read = 0;
    unsigned int before;
    unsigned int interval;

    while (total_read < size) {
        before = timeUs();
        ssize_t bytes_read = read(sock, buffer + total_read, size - total_read);
        interval = timeUs() - before;
        printf("iteration %d decoder %d: bytes_read = %d, interval = %dus\n", e, d, bytes_read, interval);
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

ssize_t send_all(int sock, const char* data, size_t size, int e, int d) {
    size_t total_sent = 0;
    unsigned int before;
    unsigned int interval;
    while (total_sent < size) {
        before = timeUs();
        ssize_t bytes_sent = send(sock, data + total_sent, size - total_sent, 0);
        interval = timeUs() - before;
        printf("iteration %d decoder %d: bytes_sent = %d, interval = %dus\n", e, d, bytes_sent, interval);
        if (bytes_sent < 0) {
            perror("Send error");
            return -1;
        }
        total_sent += bytes_sent;
    }
    return total_sent;
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
    // disable nagle algorithm
    // int flag = 1;
    // if (setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
    //     perror("setsockopt(TCP_NODELAY) failed");
    // }
    // int buff_size = 1 * 1024 * 1024; // 1MB, for example
    // setsockopt(server_fd, SOL_SOCKET, SO_SNDBUF, &buff_size, sizeof(buff_size));
    // setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &buff_size, sizeof(buff_size));

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

    unsigned int before1;
    unsigned int before2;
    unsigned int interval1;
    unsigned int interval2;
    unsigned int sum_interval1 = 0;
    unsigned int sum_interval2;
    unsigned int sum_interval3;
    
    // Main loop to handle reading and writing for all clients
    for (int e = 0; e < 50; ++e) { // Iterate multiple times as per the original logic
        sum_interval1 = 0;
        sum_interval2 = 0;
        sum_interval3 = 0;
        for (int i = 0; i < iterations; ++i) {
            memset(data, 'A' + i % 26, data_size);
            before1 = timeUs();
            for (int client_socket : client_sockets) {
                // before2 = timeUs();
                // size_t bytes_send = send(client_socket, data, data_size, 0);
                size_t bytes_send = send_all(client_socket, data, data_size, e, i);
                // interval2 = timeUs() - before2;
                // printf("iteration %d decoder %d: bytes_send = %d, interval_send = %dus\n", e, i, bytes_send, interval2);
            }
            // sum_interval2 += interval2;
            // Read from all connected clients

            for (int client_socket : client_sockets) {
                // before2 = timeUs();
                // size_t bytes_read = read(client_socket, buffer, data_size);
                size_t bytes_read = read_all(client_socket, buffer, data_size, e, i);
                // interval2 = timeUs() - before2;
                // printf("iteration %d decoder %d: bytes_read = %d, interval_read = %dus\n", e, i, bytes_read, interval2);
            }
            // sum_interval3 += interval2;
            // printf("current recieve data = %c\n", buffer[data_size - 1]);
            interval1 = timeUs() - before1;
            sum_interval1 += interval1;
            printf("iteration %d decoder %d: interval = %dus\n", e, i, interval1);
            printf("==============================================================\n");
        }
        // printf("iteration %d's send time = %d ms, read time = %d ms\n", e, sum_interval2 / 1000, sum_interval3 / 1000);
        printf("iteration %d' Time = %d ms\n\n", e, sum_interval1 / 1000);
        // if (e > 10) {
        //     sum_interval += interval;
        //     printf("iteration %d'sAveraged Time = %d ms\n\n", e, sum_interval / 1000 / (e - 10));
        // }
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
