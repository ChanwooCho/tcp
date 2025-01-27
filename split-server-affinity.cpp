#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <sys/time.h>
#include <thread>
#include <chrono>
#include <sched.h>

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

    int data_size = atoi(argv[1]) * 1024;
    int iterations = atoi(argv[2]) * 2;
    int num_clients = atoi(argv[3]);
    int port = atoi(argv[4]);

    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    std::vector<int> client_sockets;

    char* buffer = new char[data_size];
    char* data = new char[data_size];

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "Socket failed" << std::endl;
        delete[] buffer;
        delete[] data;
        return -1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        std::cerr << "setsockopt failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }

    std::cout << "Waiting for connections on port " << port << "..." << std::endl;

    fd_set read_fds;
    while (client_sockets.size() < num_clients) {
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_sd = server_fd;

        int activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);

        if (activity < 0 && errno != EINTR) {
            std::cerr << "Select error" << std::endl;
            break;
        }

        if (FD_ISSET(server_fd, &read_fds)) {
            int new_socket;
            sockaddr_in address;
            socklen_t addrlen = sizeof(address);
            if ((new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen)) < 0) {
                std::cerr << "Accept failed" << std::endl;
                close(server_fd);
                delete[] buffer;
                delete[] data;
                return -1;
            }
            client_sockets.push_back(new_socket);
        }
    }

    std::cout << "Minimum " << num_clients << " clients connected. Starting main loop." << std::endl;

    for (int e = 0; e < 50; ++e) {
        unsigned int sum_interval1 = 0;
        for (int i = 0; i < iterations; ++i) {
            memset(data, 'A' + i % 26, data_size);
            unsigned int before1 = timeUs();

            std::vector<std::thread> send_threads;
            for (int client_idx = 0; client_idx < client_sockets.size(); ++client_idx) {
                int client_socket = client_sockets[client_idx];
                
                // Create 7 threads for 1448-byte chunks
                for (int j = 0; j < 7; j++) {
                    send_threads.emplace_back([client_socket, data, e, i, j, client_idx]() {
                        // Calculate CPU core (0-3) based on combined index
                        int cpu_core = (client_idx * 7 + j) % 4;
                        cpu_set_t cpuset;
                        CPU_ZERO(&cpuset);
                        CPU_SET(cpu_core, &cpuset);
                        
                        if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == -1) {
                            perror("sched_setaffinity failed");
                        }
                        send_all(client_socket, data, 1448, e, i);
                    });
                }

                // Create thread for 104-byte chunk
                send_threads.emplace_back([client_socket, data, e, i, client_idx]() {
                    // Always use core 3 for final chunk
                    cpu_set_t cpuset;
                    CPU_ZERO(&cpuset);
                    CPU_SET(3, &cpuset);
                    
                    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == -1) {
                        perror("sched_setaffinity failed");
                    }
                    send_all(client_socket, data, 104, e, i);
                });
            }

            // Wait for all send threads
            for (auto& thread : send_threads) {
                if (thread.joinable()) {
                    thread.join();
                }
            }

            // Read phase remains sequential
            for (int client_socket : client_sockets) {
                read_all(client_socket, buffer, 10240, e, i);
            }

            sum_interval1 += timeUs() - before1;
        }
        printf("iteration %d' Time = %d ms\n\n", e, sum_interval1 / 1000);
    }

    // Cleanup
    for (int client_socket : client_sockets) {
        close(client_socket);
    }
    close(server_fd);
    delete[] buffer;
    delete[] data;

    std::cout << "Connection closed" << std::endl;
    return 0;
}
