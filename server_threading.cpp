#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>  // For atoi()
#include <vector>
#include <algorithm> // For std::max
#include <sys/time.h>
#include <thread>
#include <mutex>

// ----------------------------------------------------------------------
// Time utility
// ----------------------------------------------------------------------
unsigned long timeUs() {
    struct timeval te;
    gettimeofday(&te, NULL);
    return static_cast<unsigned long>(te.tv_sec) * 1000000LL + te.tv_usec;
}

// ----------------------------------------------------------------------
// Read all data utility
// ----------------------------------------------------------------------
ssize_t read_all(int sock, char* buffer, size_t size, int e, int d) {
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

// ----------------------------------------------------------------------
// Send all data utility
// ----------------------------------------------------------------------
ssize_t send_all(int sock, const char* data, size_t size, int e, int d) {
    size_t total_sent = 0;
    while (total_sent < size) {
        unsigned int before = timeUs();
        ssize_t bytes_sent = send(sock, data + total_sent, size - total_sent, 0);
        unsigned int interval = timeUs() - before;

        printf("iteration %d decoder %d: bytes_sent = %zd, interval = %dus\n",
               e, d, bytes_sent, interval);

        if (bytes_sent < 0) {
            perror("Send error");
            return -1;
        }
        total_sent += bytes_sent;
    }
    printf("==============================================================\n");
    return total_sent;
}

// ----------------------------------------------------------------------
// Thread function to handle a single client for one iteration
// ----------------------------------------------------------------------
void handle_client_iteration(
    int client_socket,
    const char* data,   // data to send
    size_t data_size,
    int e,              // outer loop index
    int i               // iteration index
) {
    // 1) Send data
    ssize_t bytes_sent = send_all(client_socket, data, data_size, e, i);
    if (bytes_sent < 0) {
        std::cerr << "[Thread] Send failed on client socket " << client_socket << std::endl;
        return;
    }

    // 2) Each thread has its own buffer for receiving
    std::unique_ptr<char[]> local_buffer(new char[data_size]);

    // 3) Read data
    ssize_t bytes_read = read_all(client_socket, local_buffer.get(), data_size, e, i);
    if (bytes_read < 0) {
        std::cerr << "[Thread] Read failed on client socket " << client_socket << std::endl;
        return;
    }

    // You could add checks or print statements here to verify `local_buffer`
    // std::cout << "[Thread] Last received byte for iteration " << i 
    //           << " is " << local_buffer[data_size - 1] << std::endl;
}

// ----------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: server <data_size(KB)> <# of decoders> <# of clients> <port>" << std::endl;
        return -1;
    }

    // Extract command-line arguments
    int data_size  = std::atoi(argv[1]) * 1024; // Convert the data size (KB) to bytes
    int iterations = std::atoi(argv[2]) * 2;    // #decoders * 2
    int num_clients = std::atoi(argv[3]);       // Number of clients to wait for
    int port       = std::atoi(argv[4]);        // Port

    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    fd_set read_fds;
    std::vector<int> client_sockets;

    // Allocate data buffers
    char* data = new char[data_size];

    // Create a server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "Socket failed" << std::endl;
        delete[] data;
        return -1;
    }

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        std::cerr << "setsockopt failed" << std::endl;
        close(server_fd);
        delete[] data;
        return -1;
    }

    // Prepare server address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Bind to any local IP
    address.sin_port = htons(port);

    // Bind
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        delete[] data;
        return -1;
    }

    // Listen
    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        delete[] data;
        return -1;
    }

    std::cout << "Waiting for connections on port " << port << "..." << std::endl;

    // Accept exactly num_clients connections
    while (static_cast<int>(client_sockets.size()) < num_clients) {
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        int activity = select(server_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0 && errno != EINTR) {
            std::cerr << "Select error" << std::endl;
            break;
        }

        // Check if there's a new connection
        if (FD_ISSET(server_fd, &read_fds)) {
            int new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
            if (new_socket < 0) {
                std::cerr << "Accept failed" << std::endl;
                close(server_fd);
                delete[] data;
                return -1;
            }
            std::cout << "New client connected." << std::endl;
            client_sockets.push_back(new_socket);
        }

        std::cout << "Waiting for " << num_clients 
                  << " clients. Currently connected: " 
                  << client_sockets.size() << std::endl;
    }

    std::cout << "Minimum " << num_clients << " clients connected. Starting main loop." << std::endl;

    // Outer loop (50 times, per your original code)
    for (int e = 0; e < 50; ++e) {
        unsigned int start_time = timeUs();

        // We'll do 'iterations' for each connected client
        for (int i = 0; i < iterations; ++i) {
            // Prepare the data to send (just fill with some pattern)
            memset(data, 'A' + i % 26, data_size);

            // ---------------------------
            // MULTITHREADING PART
            // ---------------------------
            // For each client, spawn a thread to send + receive
            std::vector<std::thread> threads;
            threads.reserve(client_sockets.size());

            for (int client_socket : client_sockets) {
                threads.emplace_back(
                    handle_client_iteration,
                    client_socket,
                    data,
                    data_size,
                    e,   // outer loop index
                    i    // iteration index
                );
            }

            // Wait for all threads to finish before next iteration
            for (auto& t : threads) {
                t.join();
            }
        }

        // Measure time for this outer loop 'e'
        unsigned int elapsed = timeUs() - start_time;
        std::cout << "iteration " << e 
                  << " took " << (elapsed / 1000) << " ms" << std::endl;
    }

    // Clean up
    for (int client_socket : client_sockets) {
        close(client_socket);
    }
    close(server_fd);
    delete[] data;

    std::cout << "All connections closed, server shutting down." << std::endl;
    return 0;
}
