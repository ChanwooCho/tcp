#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>  // for atoi

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
    

    for (int e = 0; e < 50; ++e) {
        for (int i = 0; i < iterations; ++i) {
            // Receive data_size KB data from server
            read(sock, buffer, data_size);
            
            // Send data_size KB data to server
            send(sock, data, data_size, 0);
        }
    }

    // Close socket
    close(sock);
    
    // Free dynamically allocated memory
    delete[] buffer;
    delete[] data;

    return 0;
}
