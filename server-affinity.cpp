#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>    // atoi()
#include <vector>
#include <thread>
#include <barrier>    // C++20 barrier
#include <sched.h>    // CPU_SET
#include <pthread.h>  // pthread_setaffinity_np
#include <sys/time.h> // gettimeofday
#include <cstdio>     // printf, perror

// 마이크로초 단위 시간 구하기
unsigned long timeUs() {
    struct timeval te;
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

// 전체 읽기
ssize_t read_all(int sock, char* buf, size_t sz, int e, int i) {
    size_t total = 0;
    while (total < sz) {
        unsigned long t0 = timeUs();
        ssize_t n = read(sock, buf + total, sz - total);
        unsigned long dt = timeUs() - t0;
        printf("[e%d it%d sock%d] read=%zd dt=%luus\n", e, i, sock, n, dt);
        if (n < 0) { perror("read"); return -1; }
        total += n;
    }
    return total;
}

// 전체 쓰기
ssize_t send_all(int sock, const char* buf, size_t sz, int e, int i) {
    size_t total = 0;
    while (total < sz) {
        unsigned long t0 = timeUs();
        ssize_t n = send(sock, buf + total, sz - total, 0);
        unsigned long dt = timeUs() - t0;
        printf("[e%d it%d sock%d] send=%zd dt=%luus\n", e, i, sock, n, dt);
        if (n < 0) { perror("send"); return -1; }
        total += n;
    }
    return total;
}

int main(int argc, char* argv[]) {
    if (argc != 7) {
        std::cerr << "Usage: server <base core> <data size> <#decoders*2> <#clients> <port> <core offset>\n";
        return -1;
    }
    int base_core   = std::atoi(argv[1]);
    int data_size   = std::atoi(argv[2]);
    int iterations  = std::atoi(argv[3]); // 이미 *2 한 값
    int num_clients = std::atoi(argv[4]);
    int port        = std::atoi(argv[5]);
    int core_off    = std::atoi(argv[6]);

    // 서버 소켓 만들기
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return -1; }
    int opt = 1;
    // SO_REUSEADDR, SO_REUSEPORT 분리 호출
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return -1; }
    if (listen(server_fd, 10) < 0) { perror("listen"); return -1; }

    std::cout << "Waiting for " << num_clients << " clients on port " << port << "...\n";

    // 클라이언트 연결 대기
    std::vector<int> socks;
    while ((int)socks.size() < num_clients) {
        int s = accept(server_fd, NULL, NULL);
        if (s < 0) { perror("accept"); continue; }
        std::cout << "Client connected: sock=" << s << "\n";
        socks.push_back(s);
    }
    std::cout << "All clients connected. Launch threads...\n";

    // 에폭/반복 동기화 장치
    std::barrier sync_point(num_clients);

    // 스레드 실행
    std::vector<std::thread> threads;
    for (int idx = 0; idx < num_clients; ++idx) {
        int sock = socks[idx];
        threads.emplace_back([=,&sync_point]() {
            // 이 스레드를 특정 코어에 묶기
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            int core = base_core + idx * core_off;
            CPU_SET(core, &cpuset);
            if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0) {
                perror("set affinity");
            } else {
                printf("Thread sock%d bound to core %d\n", sock, core);
            }

            // 데이터 버퍼 준비
            std::vector<char> data(data_size), buf(data_size);

            // 50 에폭마다 평균 계산
            for (int e = 0; e < 50; ++e) {
                unsigned long sum = 0;
                for (int i = 0; i < iterations; ++i) {
                    // 보내기 전 대기
                    sync_point.arrive_and_wait();
                    unsigned long t0 = timeUs();
                    send_all(sock, data.data(), data.size(), e, i);

                    // 읽기 전 대기
                    sync_point.arrive_and_wait();
                    read_all(sock, buf.data(), buf.size(), e, i);
                    unsigned long t1 = timeUs();

                    // 다음 반복 전 대기
                    sync_point.arrive_and_wait();
                    sum += (t1 - t0);
                }
                unsigned long avg = sum / iterations;
                printf("[sock %d] epoch %d avg = %lu us (%.3f ms)\n",
                       sock, e, avg, avg / 1000.0);
            }
            close(sock);
        });
    }

    // 모두 끝날 때까지 기다림
    for (auto &t : threads) t.join();
    close(server_fd);
    std::cout << "Server exit\n";
    return 0;
}
