#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>  // for atoi

unsigned long timeUs() {
    struct timeval te; 
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

ssize_t read_all(int sock, char* buffer, size_t size, int e) {
    size_t total_read = 0;
    unsigned int before;
    unsigned int interval;

    before = timeUs();
    while (total_read < size) {
        ssize_t bytes_read = read(sock, buffer + total_read, size - total_read);
        if (e >= 45)
            interval = timeUs() - before;
            printf("iteration %d : bytes_read = %d, interval = %dus\n", e, bytes_read, interval);
            before = timeUs();
        if (bytes_read < 0) {
            perror("Read error");
            return -1;
        } else if (bytes_read == 0) {
            // Connection closed
            break;
        }
        total_read += bytes_read;
    }
    printf("===============================\n");
    return total_read;
}

ssize_t send_all(int sock, const char* data, size_t size, int e) {
    size_t total_sent = 0;
    while (total_sent < size) {
        ssize_t bytes_sent = send(sock, data + total_sent, size - total_sent, 0);
        if (e >= 45)
            printf("iteration %d : bytes_sent = %d\n", e, bytes_sent); 
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

    // Fill the data buffer with some data
    memset(data, 'A', data_size);

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Socket creation error" << std::endl;
        delete[] buffer;
        delete[] data;
        return -1;
    }

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


    for (int e = 0; e < 50; ++e) {
        for (int i = 0; i < iterations; ++i) {
            ssize_t bytes_received = read_all(sock, buffer, data_size, e);
            if (bytes_received != data_size) {
                std::cerr << "Failed to receive full data_size bytes" << std::endl;
                // Handle error (e.g., retry, exit, etc.)
                break;
            }
            
            // Send data_size bytes to server
            ssize_t bytes_sent = send_all(sock, data, data_size, e);
            if (bytes_sent != data_size) {
                std::cerr << "Failed to send full data_size bytes" << std::endl;
                // Handle error
                break;
            }
        }
    }

    // Close socket
    close(sock);
    
    // Free dynamically allocated memory
    delete[] buffer;
    delete[] data;

    return 0;
}
