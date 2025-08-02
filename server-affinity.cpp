#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>  // atoi()
#include <vector>
#include <thread>
#include <sched.h>  // CPU affinity

unsigned long timeUs() {
    struct timeval te;
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

ssize_t read_all(int sock, char* buffer, size_t size, int e, int d) {
    size_t total_read = 0;
    while (total_read < size) {
        unsigned int before = timeUs();
        ssize_t bytes_read = read(sock, buffer + total_read, size - total_read);
        unsigned int interval = timeUs() - before;
        printf("iteration %d decoder %d: bytes_read = %zd, interval = %dus\n", e, d, bytes_read, interval);
        if (bytes_read < 0) {
            perror("Read error");
            return -1;
        }
        total_read += bytes_read;
    }
    return total_read;
}

ssize_t send_all(int sock, const char* data, size_t size, int e, int d) {
    size_t total_sent = 0;
    while (total_sent < size) {
        unsigned int before = timeUs();
        ssize_t bytes_sent = send(sock, data + total_sent, size - total_sent, 0);
        unsigned int interval = timeUs() - before;
        printf("iteration %d decoder %d: bytes_sent = %zd, interval = %dus\n", e, d, bytes_sent, interval);
        if (bytes_sent < 0) {
            perror("Send error");
            return -1;
        }
        total_sent += bytes_sent;
    }
    return total_sent;
}

int main(int argc, char* argv[]) {
    if (argc != 6) {
        std::cerr << "Usage: server <core index> <data_size(Bytes)> <# of decoders> <# of clients> <port>\n";
        return -1;
    }
    int core_index = std::atoi(argv[1]);
    // CPU affinity 설정
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_index, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
        perror("sched_setaffinity");
        return -1;
    }

    int data_size   = std::atoi(argv[2]);
    int iterations  = std::atoi(argv[3]) * 2;
    int num_clients = std::atoi(argv[4]);
    int port        = std::atoi(argv[5]);

    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    // 리스닝 소켓 생성
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "Socket failed\n";
        return -1;
    }
    // 주소 재사용 및 포트
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n";
        return -1;
    }
    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed\n";
        return -1;
    }
    std::cout << "Waiting for " << num_clients << " clients on port " << port << "...\n";

    // 클라이언트 연결 수락
    std::vector<int> client_sockets;
    while ((int)client_sockets.size() < num_clients) {
        int new_sock = accept(server_fd, NULL, NULL);
        if (new_sock < 0) {
            perror("Accept failed");
            continue;
        }
        std::cout << "Client connected: socket " << new_sock << "\n";
        client_sockets.push_back(new_sock);
    }

    std::cout << "All clients connected. Launching threads...\n";

    // 각 클라이언트별 쓰레드 실행
    std::vector<std::thread> threads;
    for (int sock : client_sockets) {
        threads.emplace_back([sock, data_size, iterations]() {
            std::vector<char> data(data_size);
            std::vector<char> buffer(data_size);
            for (int e = 0; e < 50; ++e) {
                unsigned int sum_interval = 0;
                for (int i = 0; i < iterations; ++i) {
                    memset(data.data(), 'A' + i % 26, data_size);
                    unsigned int before = timeUs();
                    send_all(sock, data.data(), data_size, e, i);
                    read_all(sock, buffer.data(), data_size, e, i);
                    unsigned int interval = timeUs() - before;
                    sum_interval += interval;
                    printf("[sock %d] iter %d dec %d: %dus\n", sock, e, i, interval);
                }
                printf("[sock %d] iteration %d total: %d ms\n", sock, e, sum_interval/1000);
            }
            close(sock);
        });
    }
    // 모든 쓰레드 종료 대기
    for (auto &t : threads) t.join();

    close(server_fd);
    std::cout << "Server shut down.\n";
    return 0;
}
