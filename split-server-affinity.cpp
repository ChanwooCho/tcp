#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>   // For atoi() and malloc()
#include <vector>
#include <sys/time.h>
#include <thread>
#include <sys/syscall.h>   // For syscall() and SYS_gettid
#include <sched.h>         // For sched_setaffinity

// Get current time in microseconds.
unsigned long timeUs() {
    struct timeval te; 
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

// Read all bytes from the socket.
ssize_t read_all(int sock, char* buffer, size_t size, int e, int d) {
    size_t total_read = 0;
    unsigned int before;
    unsigned int interval;

    while (total_read < size) {
        before = timeUs();
        ssize_t bytes_read = read(sock, buffer + total_read, size - total_read);
        interval = timeUs() - before;
        printf("iteration %d decoder %d: bytes_read = %zd, interval = %dus\n", e, d, bytes_read, interval);
        if (bytes_read < 0) {
            perror("Read error");
            return -1;
        }
        total_read += bytes_read;
    }
    return total_read;
}

// Send all bytes to the socket.
ssize_t send_all(int sock, const char* data, size_t size, int e, int d) {
    size_t total_sent = 0;
    unsigned int before;
    unsigned int interval;
    while (total_sent < size) {
        before = timeUs();
        ssize_t bytes_sent = send(sock, data + total_sent, size - total_sent, 0);
        interval = timeUs() - before;
        printf("iteration %d decoder %d: bytes_sent = %zd, interval = %dus\n", e, d, bytes_sent, interval);
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
        std::cerr << "Usage: server <data_size(Bytes)> <# of decoders> <# of clients> <port>" << std::endl;
        return -1;
    }

    // Get command-line arguments.
    int data_size = atoi(argv[1]);        // Total number of bytes to send.
    int iterations = atoi(argv[2]) * 2;     // Number of iterations.
    int num_clients = atoi(argv[3]);        // Number of clients to wait for.
    int port = atoi(argv[4]);               // Port number.

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    fd_set read_fds;
    std::vector<int> client_sockets;

    // Allocate buffers for sending and receiving.
    char* buffer = new char[data_size];
    char* data = new char[data_size];

    // Create a socket.
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "Socket failed" << std::endl;
        delete[] buffer;
        delete[] data;
        return -1;
    }

    // Allow the port to be reused.
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        std::cerr << "setsockopt failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Bind to any local IP address.
    address.sin_port = htons(port);

    // Bind the socket.
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }

    // Listen for incoming connections.
    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }

    std::cout << "Waiting for connections on port " << port << "..." << std::endl;

    // Accept connections until the required number of clients connect.
    while (client_sockets.size() < static_cast<size_t>(num_clients)) {
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_sd = server_fd;

        int activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);

        if (activity < 0 && errno != EINTR) {
            std::cerr << "Select error" << std::endl;
            break;
        }

        if (FD_ISSET(server_fd, &read_fds)) {
            if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
                std::cerr << "Accept failed" << std::endl;
                close(server_fd);
                delete[] buffer;
                delete[] data;
                return -1;
            }
            std::cout << "New client connected." << std::endl;
            client_sockets.push_back(new_socket);
        }
        std::cout << "Waiting for " << num_clients << " clients. Currently connected: " 
                  << client_sockets.size() << std::endl;
    }

    std::cout << "Minimum " << num_clients << " clients connected. Starting main loop." << std::endl;

    unsigned int before1;
    unsigned int interval1;
    unsigned int sum_interval1 = 0;
    
    // Main loop for multiple iterations.
    for (int e = 0; e < 50; ++e) {
        sum_interval1 = 0;
        for (int i = 0; i < iterations; ++i) {
            // Fill the data buffer with a repeating character.
            memset(data, 'A' + i % 26, data_size);
            before1 = timeUs();

            // Send data to each connected client.
            for (int client_socket : client_sockets) {
                int part_size = data_size / 4; // Split data into 4 parts.
                std::vector<std::thread> threads;
                for (int t = 0; t < 4; ++t) {
                    threads.push_back(std::thread([client_socket, data, part_size, e, i, t]() {
                        // Create a CPU set and add one CPU (core t+1).
                        cpu_set_t cpuset;
                        CPU_ZERO(&cpuset);
                        CPU_SET(t + 1, &cpuset);

                        // Get the thread ID using syscall.
                        pid_t tid = syscall(SYS_gettid);
                        // Set the CPU affinity using sched_setaffinity.
                        int rc = sched_setaffinity(tid, sizeof(cpu_set_t), &cpuset);
                        if (rc != 0) {
                            std::cerr << "Error setting thread affinity for thread " << t + 1 << std::endl;
                        }
                        // Send this part of the data.
                        ssize_t sent = send_all(client_socket, data + t * part_size, part_size, e, t + 1);
                        if (sent < 0) {
                            std::cerr << "Error in send_all in thread " << t + 1 << std::endl;
                        }
                    }));
                }
                // Wait for all four threads to finish.
                for (auto &th : threads) {
                    th.join();
                }
            }
            // Read data from all connected clients.
            for (int client_socket : client_sockets) {
                ssize_t bytes_read = read_all(client_socket, buffer, data_size, e, i);
                if (bytes_read < 0) {
                    std::cerr << "Error reading from client socket" << std::endl;
                }
            }
            interval1 = timeUs() - before1;
            sum_interval1 += interval1;
            printf("iteration %d decoder %d: total interval = %dus\n", e, i, interval1);
            printf("==============================================================\n");
        }
        printf("iteration %d total Time = %d ms\n\n", e, sum_interval1 / 1000);
    }

    // Clean up: close all client sockets and the server socket.
    for (int client_socket : client_sockets) {
        close(client_socket);
    }
    close(server_fd);
    delete[] buffer;
    delete[] data;

    std::cout << "Connection closed" << std::endl;

    return 0;
}
