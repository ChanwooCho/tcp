#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <netinet/in.h>
#include <netinet/tcp.h> 
#include <thread>
#include <iostream>
#include <chrono> 
#include <sched.h>

// For Android Core Affinity
#if defined(__ANDROID__)
    #if defined(__x86_64__)
    #define __NR_sched_setaffinity 203
    #elif defined(__arm__)
    #define __NR_sched_setaffinity 241
    #elif defined(__aarch64__)
    #define __NR_sched_setaffinity 122
    #endif
    #define CPU_SETSIZE 1024
    #define __NCPUBITS (8 * sizeof (unsigned long))
    typedef struct {
        unsigned long __bits[CPU_SETSIZE / __NCPUBITS];
    } cpu_set_t;

    void CPU_ZERO(cpu_set_t *set) {
        memset(set, 0, sizeof(cpu_set_t));
    }

    void CPU_SET(int cpu, cpu_set_t *set) {
        set->__bits[cpu / __NCPUBITS] |= (1UL << (cpu % __NCPUBITS));
    }

    // Define sched_setaffinity using syscall
    int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask) {
        int result = syscall(__NR_sched_setaffinity, pid, cpusetsize, mask);
        if (result != 0) {
            errno = result;
            return -1;
        }
        return 0;
    }
#endif

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
    unsigned int is_first = 1;
    
    while (total_read < size) {
        before = timeUs();
        ssize_t bytes_read = read(sock, buffer + total_read, size - total_read);
        interval = timeUs() - before;
        if (min_latency > interval && is_first) {
            min_latency = interval;
        }
        is_first = 0;
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
        printf("iteration %d decoder %d: bytes_send = %d, interval = %dus\n", e, d, bytes_sent, interval);
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
        std::cerr << "Usage: client <data_size(KB)> <# of decoders> <ip_address:port>" << std::endl;
        return -1;
    }

    // Parse data_size and iterations
    int data_size = std::atoi(argv[1]) * 1024; // Bytes
    int iterations = std::atoi(argv[2]) * 2; // attention layer + feedforward layer

    // Split the IP address and port
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

    // disable nagle algorithm
    // int flag = 1;
    // if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
    //     perror("setsockopt(TCP_NODELAY) failed");
    // }
    // int buff_size = 1 * 1024 * 1024; // 1MB, for example
    // setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buff_size, sizeof(buff_size));
    // setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buff_size, sizeof(buff_size));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Convert IPv4 and IPv6 addresses from text to binary form
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
    
    // 기존 코드
    // for (int e = 0; e < 50; ++e) {
    //     for (int i = 0; i < iterations; ++i) {
    //         // Receive data_size KB data from server
    //         ssize_t read_size = read(sock, buffer, data_size);
    //         // printf("read = %d\n", read_size); 
            
    //         // Send data_size KB data to server
    //         ssize_t send_size = send(sock, data, data_size, 0);
    //         // printf("send = %d\n", send_size);
    //     }
    // }

    // CORE AFFINITY
    int cpu_index = 3; 
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_index, &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);

    unsigned int before1;
    unsigned int before2;
    unsigned int interval1;
    unsigned int interval2;
    unsigned int sum_interval1 = 0;
    unsigned int sum_interval2;
    unsigned int sum_interval3;
    for (int e = 0; e < 50; ++e) {
        for (int i = 0; i < iterations; ++i) {
            memset(data, 'A' + i % 26, data_size);
            before1 = timeUs();
            
            read_all(sock, buffer, 10240, e, i);

            for (int j = 0; j < 7; j++){
                // std::this_thread::sleep_for(std::chrono::microseconds(234 * 2));
                send_all(sock, data, 1448 * 1, e, i);
            }
            // std::this_thread::sleep_for(std::chrono::microseconds(234 + 17));
            send_all(sock, data, 104, e, i);

            interval1 = timeUs() - before1;
            printf("iteration %d decoder %d: interval = %dus\n", e, i, interval1);
            printf("==============================================================\n");
        }
    }
    

    // Close socket
    close(sock);
    
    // Free dynamically allocated memory
    delete[] buffer;
    delete[] data;

    return 0;
}
