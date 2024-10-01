
#include <iostream>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>  // for atoi

#define DATA_SIZE 20480  // 20KB
#define ITERATIONS 160

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

    unsigned int before = timeUs();
    for (int i = 0; i < ITERATIONS; ++i) {
        // Send 20KB data to server
        if (send(sock, data, DATA_SIZE, 0) < 0) {
            std::cerr << "Send failed" << std::endl;
            break;
        }
        std::cout << "Sent 20KB to server: Iteration " << i + 1 << std::endl;

        // Receive 20KB data from server
        if (read(sock, buffer, DATA_SIZE) < 0) {
            std::cerr << "Read failed" << std::endl;
            break;
        }
        std::cout << "Received 20KB from server: Iteration " << i + 1 << std::endl;
    }
    unsigned int interval = timeUs() - before;
    // Close socket
    close(sock);
    std::cout << "Connection close and Duration is " << interval << "us" << std::endl;

    return 0;
}
