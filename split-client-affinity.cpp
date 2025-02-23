#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <thread>
#include <vector>
#include <pthread.h>
#include <sys/time.h>
#include <string>

// Returns current time in microseconds
unsigned long timeUs() {
    struct timeval te; 
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

unsigned int min_latency;

ssize_t read_all(int sock, char* buffer, size_t size, int e, int d) {
    size_t total_read = 0;
    unsigned int before;
    unsigned int interval;
    bool is_first = true;
    
    while (total_read < size) {
        before = timeUs();
        ssize_t bytes_read = read(sock, buffer + total_read, size - total_read);
        interval = timeUs() - before;
        if (min_latency > interval && is_first) {
            min_latency = interval;
        }
        is_first = false;
        printf("iteration %d decoder %d: bytes_read = %zd, interval = %dus\n", e, d, bytes_read, interval);

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
        printf("iteration %d decoder %d: bytes_sent = %zd, interval = %dus\n", e, d, bytes_sent, interval);
        if (bytes_sent < 0) {
            perror("Send error");
            return -1;
        }
        total_sent += bytes_sent;
    }
    return total_sent;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: client <data_size(Bytes)> <# of decoders> <ip_address:port>" << std::endl;
        return -1;
    }

    // Parse the data size and number of decoders.
    int data_size = std::atoi(argv[1]); // total bytes to send
    int decoders = std::atoi(argv[2]);  // number of decoders (this was used earlier to calculate iterations)
    int iterations = decoders * 2;       // still using the same inner loop count

    // Parse the IP address and port from the input argument
    std::string input(argv[3]);
    std::size_t colon_pos = input.find(':');
    if (colon_pos == std::string::npos) {
        std::cerr << "Invalid argument format. Use: <ip address:port>" << std::endl;
        return -1;
    }
    std::string ip_address = input.substr(0, colon_pos);
    int port = std::atoi(input.substr(colon_pos + 1).c_str());

    int sock = 0;
    struct sockaddr_in serv_addr;
    char *buffer = new char[data_size];
    char *data = new char[data_size];

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Socket creation error" << std::endl;
        delete[] buffer;
        delete[] data;
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Convert IP address from text to binary form
    if (inet_pton(AF_INET, ip_address.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        delete[] buffer;
        delete[] data;
        return -1;
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection Failed" << std::endl;
        delete[] buffer;
        delete[] data;
        return -1;
    }

    std::cout << "Connected to server at " << ip_address << ":" << port << std::endl;
    
    // Main loop: 50 outer iterations, and inner loop based on iterations count.
    for (int e = 0; e < 50; ++e) {
        for (int i = 0; i < iterations; ++i) {
            // Fill the data buffer with a repeating character
            memset(data, 'A' + i % 26, data_size);

            // Receive data from the server
            ssize_t bytes_received = read_all(sock, buffer, data_size, e, i);
            if (bytes_received < 0) {
                std::cerr << "Error in read_all" << std::endl;
                break;
            }

            // Instead of sending the whole data at once, split it into 4 parts.
            // Each part will be sent by a separate thread pinned to a specific CPU core.
            int part_size = data_size / 4; // assume data_size is divisible by 4
            std::vector<std::thread> threads;

            for (int t = 0; t < 4; ++t) {
                threads.push_back(std::thread([sock, data, part_size, e, t]() {
                    // Set the thread affinity to a specific core (cores 1, 2, 3, 4)
                    cpu_set_t cpuset;
                    CPU_ZERO(&cpuset);
                    CPU_SET(t + 1, &cpuset); // core index: t+1
                    pthread_t current_thread = pthread_self();
                    int rc = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
                    if (rc != 0) {
                        std::cerr << "Error setting thread affinity for thread " << t + 1 << std::endl;
                    }
                    // Send this part of the data
                    ssize_t sent = send_all(sock, data + t * part_size, part_size, e, t + 1);
                    if (sent < 0) {
                        std::cerr << "Error in send_all in thread " << t + 1 << std::endl;
                    }
                }));
            }
            // Wait for all four threads to finish sending
            for (auto &th : threads) {
                th.join();
            }

            printf("Completed iteration %d, inner loop %d\n", e, i);
            printf("==============================================================\n");
        }
    }
    
    // Close the socket and free memory
    close(sock);
    delete[] buffer;
    delete[] data;

    return 0;
}
