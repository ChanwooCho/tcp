#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include <sched.h>  // for CPU affinity

// Get current time in microseconds
unsigned long timeUs() {
    struct timeval te;
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

unsigned int min_latency;
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

int main(int argc, char *argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: client <core index(0-7)> <data_size(Bytes)> <# of decoders> <chunck_size(Bytes)> <ip_address:port>" << std::endl;
        return -1;
    }

    int core_index = std::atoi(argv[1]);
    // Set CPU affinity to core 1 (the second core)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_index, &cpuset);  // use core 1
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
        perror("sched_setaffinity");
        return -1;
    }

    // Data Size & Iteration & Chunck Size
    int data_size = std::atoi(argv[2]); // size in bytes
    int iterations = std::atoi(argv[3]) * 2;  // iterations count
    int chunck_size = std::atoi(argv[4]);  // iterations count

    // Split the IP address and port
    std::string input(argv[5]);
    std::size_t colon_pos = input.find(':');
    if (colon_pos == std::string::npos) {
        std::cerr << "Invalid argument format. Use: <ip_address:port>" << std::endl;
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

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection Failed" << std::endl;
        delete[] buffer;
        delete[] data;
        return -1;
    }
    std::cout << "Connected to server at " << ip_address << ":" << port << std::endl;

    // Main loop
    unsigned int before1;
    unsigned int interval1;
    for (int e = 0; e < 50; ++e) {
        for (int i = 0; i < iterations; ++i) {
            memset(data, 'A' + i % 26, data_size);
            before1 = timeUs();
            ssize_t bytes_received = read_all(sock, buffer, data_size);
            if (bytes_received < 0) {
                close(sock);
                delete[] buffer;
                delete[] data;
                return -1;
            }
            
            ssize_t bytes_sent = send_all(sock, buffer, data_size, chunck_size);
            if (bytes_sent < 0) {
                close(sock);
                delete[] buffer;
                delete[] data;
                return -1;
            }
            interval1 = timeUs() - before1;
            
            printf("iteration %d decoder %d: total interval = %dus\n", e, i, interval1);
            printf("current core index = %d\n", sched_getcpu());
            printf("==============================================================\n");
        }
    }

    // Close socket and free memory
    close(sock);
    delete[] buffer;
    delete[] data;

    return 0;
}
