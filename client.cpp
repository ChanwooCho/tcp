#include <iostream>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>  // for atoi

#define DATA_SIZE 20480  // 20KB
#define ITERATIONS 160
#define INITIAL_DATA_SIZE (200 * 1024 * 1024)  // 200MB

unsigned long timeUs() {
    struct timeval te; 
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: client <ip address:port>" << std::endl;
        return -1;
    }

    // Split the input argument into IP address and port
    std::string input(argv[1]);
    std::size_t colon_pos = input.find(':');
    if (colon_pos == std::string::npos) {
        std::cerr << "Invalid argument format. Use: <ip address:port>" << std::endl;
        return -1;
    }

    std::string ip_address = input.substr(0, colon_pos);
    int port = std::atoi(input.substr(colon_pos + 1).c_str());

    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[DATA_SIZE] = {0};
    char data[DATA_SIZE] = {0};

    // Fill the data buffer with some data
    memset(data, 'A', DATA_SIZE);

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Socket creation error" << std::endl;
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, ip_address.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        return -1;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection Failed" << std::endl;
        return -1;
    }

    std::cout << "Connected to server at " << ip_address << ":" << port << std::endl;

    // Step 1: Allocate a dynamic buffer to receive 200MB of data
    char *large_buffer = new(std::nothrow) char[INITIAL_DATA_SIZE];  // Allocate 200MB
    if (large_buffer == nullptr) {
        std::cerr << "Memory allocation failed" << std::endl;
        close(sock);
        return -1;
    }

    size_t total_received = 0;
    size_t bytes_received;
    std::cout << "Receiving 200MB of data from the server..." << std::endl;

    while (total_received < INITIAL_DATA_SIZE) {
        bytes_received = read(sock, buffer, DATA_SIZE);  // Read in chunks of 20KB
        if (bytes_received < 0) {
            std::cerr << "Read failed during 200MB reception" << std::endl;
            delete[] large_buffer;
            close(sock);
            return -1;
        }
        if (bytes_received == 0) {
            std::cerr << "Connection closed by server during 200MB reception" << std::endl;
            delete[] large_buffer;
            close(sock);
            return -1;
        }

        // Copy received data into the large_buffer
        memcpy(large_buffer + total_received, buffer, bytes_received);
        total_received += bytes_received;

        std::cout << "Received " << total_received << "/" << INITIAL_DATA_SIZE << " bytes" << std::endl;
    }
    std::cout << "Completed receiving 200MB of data from the server." << std::endl;

    // Step 2: Now start the 160 iterations of sending and receiving 20KB
    unsigned int before = timeUs();
    for (int i = 0; i < ITERATIONS; ++i) {
        // Send 20KB data to server
        if (send(sock, data, DATA_SIZE, 0) < 0) {
            std::cerr << "Send failed" << std::endl;
            break;
        }
        // Receive 20KB data from server
        if (read(sock, buffer, DATA_SIZE) < 0) {
            std::cerr << "Read failed" << std::endl;
            break;
        }
    }
    unsigned int interval = timeUs() - before;

    // Clean up and close socket
    delete[] large_buffer;  // Free the allocated memory
    close(sock);
    std::cout << "Connection closed and duration is " << interval << "us" << std::endl;

    return 0;
}
