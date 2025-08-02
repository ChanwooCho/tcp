#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>  // atoi()
#include <vector>
#include <thread>
#include <barrier>  // C++20 barrier for synchronization
#include <sched.h>   // CPU affinity
#include <pthread.h> // pthread_setaffinity_np

unsigned long timeUs() {
    struct timeval te;
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

ssize_t read_all(int sock, char* buffer, size_t size, int e, int d) {
    size_t total_read = 0;
    while (total_read < size) {
        unsigned int before = timeUs();
        ssize_t bytes_read = read(sock, buffer + total_read, size - total_read);
        unsigned int interval = timeUs() - before;
        printf("[e%d d%d sock%d] bytes_read=%zd interval=%dus\n", e, d, sock, bytes_read, interval);
        if (bytes_read < 0) {
            perror("Read error");
            return -1;
        }
        total_read += bytes_read;
    }
    return total_read;
}

ssize_t send_all(int sock, const char* data, size_t size, int e, int d) {
    size_t total_sent = 0;
    while (total_sent < size) {
        unsigned int before = timeUs();
        ssize_t bytes_sent = send(sock, data + total_sent, size - total_sent, 0);
        unsigned int interval = timeUs() - before;
        printf("[e%d d%d sock%d] bytes_sent=%zd interval=%dus\n", e, d, sock, bytes_sent, interval);
        if (bytes_sent < 0) {
            perror("Send error");
            return -1;
        }
        total_sent += bytes_sent;
    }
    return total_sent;
}

int main(int argc, char* argv[]) {
    if (argc != 7) {
        std::cerr << "Usage: server <base core index> <data_size(Bytes)> <# of decoders> <# of clients> <port> <core offset>\n";
        return -1;
    }
    int base_core    = std::atoi(argv[1]);
    int data_size    = std::atoi(argv[2]);
    int iterations   = std::atoi(argv[3]) * 2;
    int num_clients  = std::atoi(argv[4]);
    int port         = std::atoi(argv[5]);
    int core_offset  = std::atoi(argv[6]); // core index increment per thread

    // Listening socket setup
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("Socket failed"); return -1; }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) { perror("Bind failed"); return -1; }
    if (listen(server_fd, 10) < 0) { perror("Listen failed"); return -1; }
    std::cout << "Waiting for " << num_clients << " clients on port " << port << "...\n";

    // Accept clients
    std::vector<int> client_sockets;
    while ((int)client_sockets.size() < num_clients) {
        int sock = accept(server_fd, NULL, NULL);
        if (sock < 0) { perror("Accept failed"); continue; }
        std::cout << "Client connected (socket " << sock << ")\n";
        client_sockets.push_back(sock);
    }
    std::cout << "All clients connected. Starting synchronized, affinity-bound threads...\n";

    // Barrier for synchronization
    std::barrier sync_point(num_clients);

    // Launch threads with CPU affinity and epoch-average calculation
    std::vector<std::thread> threads;
    for (int idx = 0; idx < num_clients; ++idx) {
        int sock = client_sockets[idx];
        threads.emplace_back([=]() {
            // Bind this thread to a specific core
            cpu_set_t set;
            CPU_ZERO(&set);
            int target_core = base_core + idx * core_offset;
            CPU_SET(target_core, &set);
            if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0) {
                perror("pthread_setaffinity_np");
            } else {
                std::cout << "Thread for socket " << sock << " bound to core " << target_core << "\n";
            }

            std::vector<char> data(data_size);
            std::vector<char> buffer(data_size);
            for (int e = 0; e < 50; ++e) {
                unsigned long sum_interval = 0;
                for (int i = 0; i < iterations; ++i) {
                    // Prepare data
                    memset(data.data(), 'A' + (i % 26), data.size());
                    // Sync before send
                    sync_point.arrive_and_wait();
                    unsigned long t0 = timeUs();
                    send_all(sock, data.data(), data.size(), e, i);
                    // Sync before read
                    sync_point.arrive_and_wait();
                    read_all(sock, buffer.data(), buffer.size(), e, i);
                    unsigned long t1 = timeUs();
                    // Sync before next iteration
                    sync_point.arrive_and_wait();
                    unsigned long delta = t1 - t0;
                    sum_interval += delta;
                }
                unsigned long avg = sum_interval / iterations;
                printf("[sock %d] epoch %d average per iter = %lu us (%.3f ms)\n", sock, e, avg, avg/1000.0);
            }
            close(sock);
        });
    }
    // Wait for threads to finish
    for (auto &t : threads) t.join();

    close(server_fd);
    std::cout << "Server shut down.\n";
    return 0;
}
