#define _GNU_SOURCE  // sometimes needed for pthread_setaffinity_np
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
#include <pthread.h>
#include <sched.h>     // For cpu_set_t, CPU_SET, CPU_ZERO

// --------------------------------------------------
// Simple atomic counters (for thread indexing)
// --------------------------------------------------
static int g_sendThreadIndex = 0;

// --------------------------------------------------
// Utility to get current time in microseconds
// --------------------------------------------------
unsigned long timeUs() {
    struct timeval te;
    gettimeofday(&te, NULL);
    return (unsigned long)te.tv_sec * 1000000LL + te.tv_usec;
}

// --------------------------------------------------
// read_all (unchanged)
// --------------------------------------------------
ssize_t read_all(int sock, char* buffer, size_t size, int e, int d) {
    size_t total_read = 0;
    while (total_read < size) {
        unsigned long before = timeUs();
        ssize_t bytes_read = read(sock, buffer + total_read, size - total_read);
        unsigned long interval = timeUs() - before;

        printf("iteration %d decoder %d: bytes_read = %zd, interval = %luus\n",
               e, d, bytes_read, interval);

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

// --------------------------------------------------
// send_all (unchanged)
// --------------------------------------------------
ssize_t send_all(int sock, const char* data, size_t size, int e, int d) {
    size_t total_sent = 0;
    while (total_sent < size) {
        unsigned long before = timeUs();
        ssize_t bytes_sent = send(sock, data + total_sent, size - total_sent, 0);
        unsigned long interval = timeUs() - before;

        printf("iteration %d decoder %d: bytes_sent = %zd, interval = %luus\n",
               e, d, bytes_sent, interval);

        if (bytes_sent < 0) {
            perror("Send error");
            return -1;
        }
        total_sent += bytes_sent;
    }
    return total_sent;
}

// --------------------------------------------------
// Wrapper to pin the sending thread to CPU 1..3
// (round-robin) then call send_all().
// --------------------------------------------------
void threadedSendAll(int sock, const char* data, size_t size, int e, int d) {
    // Choose a CPU for sending in {1,2,3}
    int myIndex  = __sync_fetch_and_add(&g_sendThreadIndex, 1);
    int cpu_id   = 1 + (myIndex % 3);  // cycles through 1..3

    // Build CPU set
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);

    // Pin to chosen CPU
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    if (rc != 0) {
        std::cerr << "[Send Thread] Error setting affinity to CPU "
                  << cpu_id << ": " << rc << std::endl;
    } else {
        std::cout << "[Send Thread] pinned to CPU " 
                  << cpu_id << std::endl;
    }

    // Actual send
    send_all(sock, data, size, e, d);
}

// --------------------------------------------------
// The "read thread" function:
//  - pinned to CPU #0
//  - loops reading from each client
// --------------------------------------------------
void readThreadFunc(std::vector<int> client_sockets,
                    char* buffer,
                    int data_size,
                    int totalIterations)
{
    // Pin this thread to CPU #0
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);  // CPU #0
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    if (rc != 0) {
        std::cerr << "[Read Thread] Error setting affinity to CPU 0: " 
                  << rc << std::endl;
    } else {
        std::cout << "[Read Thread] pinned to CPU 0\n";
    }

    // For demonstration, let's do 'totalIterations' read cycles
    // Each cycle: read from each client one block (e.g., 10240 bytes)
    // This is just an example; adapt to your real logic.
    for (int e = 0; e < totalIterations; e++) {
        // We only have "decoder" index as e for printing. Or pass more if needed
        for (int client_socket : client_sockets) {
            // In your original code, you read 10240 each time
            read_all(client_socket, buffer, 10240, e, /*decoder=*/0);
        }
    }

    std::cout << "[Read Thread] finished all reading cycles\n";
}

// --------------------------------------------------
// Main
// --------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: server <data_size(KB)> <# of decoders> "
                  << "<# of clients> <port>\n";
        return -1;
    }

    // Parse command line
    int data_size   = std::atoi(argv[1]) * 1024;
    int decoders    = std::atoi(argv[2]);
    int num_clients = std::atoi(argv[3]);
    int port        = std::atoi(argv[4]);

    // In your original code, you did "iterations = decoders * 2".
    // We'll keep that logic or adapt:
    int iterations = decoders * 2;

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    // Prepare buffers
    char* buffer = new char[data_size];
    char* data   = new char[data_size];

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "Socket failed\n";
        return -1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt))) {
        std::cerr << "setsockopt failed\n";
        close(server_fd);
        return -1;
    }

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;  // any local IP
    address.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n";
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed\n";
        close(server_fd);
        return -1;
    }

    std::cout << "Listening on port " << port << "\n";

    // Accept clients
    std::vector<int> client_sockets;
    while ((int)client_sockets.size() < num_clients) {
        new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (new_socket < 0) {
            std::cerr << "Accept failed\n";
            close(server_fd);
            return -1;
        }
        std::cout << "Client connected\n";
        client_sockets.push_back(new_socket);
        std::cout << "Now " << client_sockets.size() 
                  << " clients connected.\n";
    }

    // ----------------------------------------------
    // 1) Start ONE read thread pinned to CPU #0
    // ----------------------------------------------
    // Suppose we want it to handle 'X' read cycles total.
    // This could match your main iteration loops, or be infinite, etc.
    int readCycles = 50;  // or whatever you want
    std::thread readThread(readThreadFunc,
                           client_sockets,
                           buffer,       // share the buffer
                           data_size,    // for example
                           readCycles);

    // ----------------------------------------------
    // 2) Meanwhile, do sending in the main thread
    //    or create sending threads as needed
    // ----------------------------------------------
    // Example: 50 iterations, each with "iterations" sub-steps
    // That’s from your original code logic.
    for (int e = 0; e < 50; ++e) {
        for (int i = 0; i < iterations; ++i) {
            // Prepare data
            memset(data, 'A' + (i % 26), data_size);

            // We create threads that call send_all pinned to CPU#1..#3
            std::vector<std::thread> sendThreads;

            for (int sockfd : client_sockets) {
                // Example: 7 sends of 1448 + 1 send of 104
                for (int j = 0; j < 7; j++) {
                    sendThreads.emplace_back([=]() {
                        threadedSendAll(sockfd, data, 1448, e, i);
                    });
                }
                sendThreads.emplace_back([=]() {
                    threadedSendAll(sockfd, data, 104, e, i);
                });
            }

            // Join the send threads
            for (auto &t : sendThreads) {
                t.join();
            }
        }
        std::cout << "[Main] Completed iteration " << e << "\n";
    }

    // ----------------------------------------------
    // 3) Wait for read thread to finish
    // ----------------------------------------------
    if (readThread.joinable()) {
        readThread.join();
    }

    // Cleanup
    for (int s : client_sockets) {
        close(s);
    }
    close(server_fd);
    delete[] buffer;
    delete[] data;

    std::cout << "Server shutting down\n";
    return 0;
}
