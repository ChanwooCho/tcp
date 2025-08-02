#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <sys/syscall.h> // syscall, SYS_gettid
#include <sched.h>       // CPU_SET, sched_setaffinity
#include <cstring>       // memset
#include <cstdlib>       // atoi
#include <vector>
#include <thread>
#include <barrier>       // C++20 barrier
#include <sys/time.h>    // gettimeofday
#include <cstdio>        // printf, perror

// 마이크로초 단위 시간 구하기
unsigned long timeUs() {
    struct timeval te;
    gettimeofday(&te, nullptr);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

// 지정한 크기만큼 모두 읽기
ssize_t read_all(int sock, char* buf, size_t sz, int e, int it) {
    size_t total = 0;
    while (total < sz) {
        unsigned long t0 = timeUs();
        ssize_t n = read(sock, buf + total, sz - total);
        unsigned long dt = timeUs() - t0;
        printf("[e%d it%d sock%d] read=%zd dt=%luus\n", e, it, sock, n, dt);
        if (n < 0) { perror("read"); return -1; }
        total += n;
    }
    return total;
}

// 지정한 크기만큼 모두 쓰기
ssize_t send_all(int sock, const char* buf, size_t sz, int e, int it) {
    size_t total = 0;
    while (total < sz) {
        unsigned long t0 = timeUs();
        ssize_t n = send(sock, buf + total, sz - total, 0);
        unsigned long dt = timeUs() - t0;
        printf("[e%d it%d sock%d] send=%zd dt=%luus\n", e, it, sock, n, dt);
        if (n < 0) { perror("send"); return -1; }
        total += n;
    }
    return total;
}

int main(int argc, char* argv[]) {
    if (argc != 7) {
        std::cerr << "Usage: server <base_core> <data_size> <#iters> <#clients> <port> <core_offset>\n";
        return -1;
    }

    int base_core   = std::atoi(argv[1]);
    int data_size   = std::atoi(argv[2]);
    int iterations  = std::atoi(argv[3]) * 2; // 총 반복 횟수
    int num_clients = std::atoi(argv[4]);
    int port        = std::atoi(argv[5]);
    int core_off    = std::atoi(argv[6]);

    // 1) 서버 소켓 만들기
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return -1; }

    int opt = 1;
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

    // 2) 클라이언트 연결 받기
    std::vector<int> socks;
    while ((int)socks.size() < num_clients) {
        int s = accept(server_fd, nullptr, nullptr);
        if (s < 0) { perror("accept"); continue; }
        std::cout << "Client connected: sock=" << s << "\n";
        socks.push_back(s);
    }
    std::cout << "All clients connected. Launching threads...\n";

    // 3) 에폭/반복 동기화 도구
    std::barrier sync_point(num_clients);

    // 4) 스레드 실행
    std::vector<std::thread> threads;
    for (int idx = 0; idx < num_clients; ++idx) {
        int sock = socks[idx];
        threads.emplace_back([=,&sync_point]() {
            // --- affinity 설정: Android용 sched_setaffinity ---
            pid_t tid = syscall(SYS_gettid);
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            int core = base_core + idx * core_off;
            CPU_SET(core, &cpuset);
            if (sched_setaffinity(tid, sizeof(cpuset), &cpuset) != 0) {
                perror("sched_setaffinity");
            } else {
                printf("Thread sock%d bound to core %d (tid %d)\n", sock, core, tid);
            }

            // 데이터 버퍼 준비
            std::vector<char> data(data_size), buf(data_size);

            // 50 에폭 동안 반복
            for (int e = 0; e < 50; ++e) {
                unsigned long sum = 0;
                unsigned long start = timeUs();
                for (int it = 0; it < iterations; ++it) {
                    // 1) 모두 보내기 전 동기화
                    // sync_point.arrive_and_wait();
                    send_all(sock, data.data(), data.size(), e, it);
                    read_all(sock, buf.data(), buf.size(), e, it);
                }
                unsigned long duration = timeUs() - start;
                printf("[sock %d] epoch %d avg = %lu us (%.3f ms)\n",
                       sock, e, duration, duration / 1000.0);
            }
            close(sock);
        });
    }

    // 5) 모든 스레드 종료 대기
    for (auto &t : threads) t.join();
    close(server_fd);
    std::cout << "Server shut down.\n";
    return 0;
}
