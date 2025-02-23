#include <iostream>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sched.h>
#include <sys/time.h>

// Function to get current time in microseconds
unsigned long timeUs() {
    struct timeval te;
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

// Modified send_all function with extra parameters for logging (thread_id, core_id)
ssize_t send_all(int sock, const char* data, size_t size, int thread_id, int core_id) {
    size_t total_sent = 0;
    unsigned int before;
    unsigned int interval;
    while (total_sent < size) {
        before = timeUs();
        ssize_t bytes_sent = send(sock, data + total_sent, size - total_sent, 0);
        interval = timeUs() - before;
        printf("Thread %d on core %d: bytes_sent = %zd, interval = %dus\n", thread_id, core_id, bytes_sent, interval);
        if (bytes_sent < 0) {
            perror("Send error");
            return -1;
        }
        total_sent += bytes_sent;
    }
    return total_sent;
}

// Function to be run in each thread.
// It binds the thread to a specific core and then calls send_all.
void send_thread_func(int sock, const char* data, size_t size, int core_id, int thread_id) {
    // Set CPU affinity for this thread to the specified core
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        perror("sched_setaffinity");
    }
    printf("Thread %d is bound to core %d\n", thread_id, core_id);
    // Call send_all for 2560 bytes
    send_all(sock, data, size, thread_id, core_id);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: client <ip_address:port> <data_size_per_thread_in_bytes>" << std::endl;
        return -1;
    }

    // Parse IP and port
    std::string input(argv[1]);
    std::size_t colon_pos = input.find(':');
    if (colon_pos == std::string::npos) {
        std::cerr << "Invalid argument format. Use: <ip_address:port>" << std::endl;
        return -1;
    }
    std::string ip_address = input.substr(0, colon_pos);
    int port = std::atoi(input.substr(colon_pos + 1).c_str());

    // Data size per thread; here we expect 2560 bytes per thread
    int send_size = std::atoi(argv[2]);
    if (send_size != 2560) {
        std::cerr << "This example is set up for 2560 bytes per thread." << std::endl;
        return -1;
    }

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation error" << std::endl;
        return -1;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip_address.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address / Address not supported" << std::endl;
        return -1;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection Failed" << std::endl;
        return -1;
    }
    std::cout << "Connected to server at " << ip_address << ":" << port << std::endl;

    // Number of threads (and cores) we want to use
    const int thread_count = 4;

    // Allocate a buffer of size 2560 bytes for each thread
    char* data_buffer = new char[send_size * thread_count];
    // Fill each thread's buffer with a distinct character for differentiation
    for (int i = 0; i < thread_count; i++) {
        memset(data_buffer + i * send_size, 'A' + i, send_size);
    }

    // Create threads. Each thread sends its own 2560-byte portion and binds to a specific core.
    std::thread threads[thread_count];
    for (int i = 0; i < thread_count; i++) {
        // core indices: 1, 2, 3, 4 (change if needed for your system)
        int core_index = i + 1;
        threads[i] = std::thread(send_thread_func, sock, data_buffer + i * send_size, send_size, core_index, i + 1);
    }

    // Wait for all threads to finish
    for (int i = 0; i < thread_count; i++) {
        threads[i].join();
    }

    // Close socket and free memory
    close(sock);
    delete[] data_buffer;
    return 0;
}
