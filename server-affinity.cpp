#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>        // fcntl, O_NONBLOCK
#include <sys/epoll.h>    // epoll
#include <sys/time.h>     // gettimeofday
#include <cstring>        // memset, strerror
#include <cstdlib>        // atoi, exit
#include <vector>
#include <cstdio>         // printf, perror
#include <errno.h>

// 마이크로초 단위 시간 측정
unsigned long timeUs() {
    struct timeval te;
    gettimeofday(&te, nullptr);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

int setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char* argv[]) {
    if (argc != 7) {
        std::cerr << "Usage: server <base_core_unused> <data_size> <#iters> <#clients> <port> <core_offset_unused>\n";
        return -1;
    }

    const int data_size  = std::atoi(argv[2]);
    const int iterations = std::atoi(argv[3]) * 2; // 원래 코드 호환
    const int num_clients= std::atoi(argv[4]);
    const int port       = std::atoi(argv[5]);
    const int EPOCHS     = 50;

    // 1) 리스닝 소켓 만들기
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    setsockopt(listen_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(listen_fd, 10) < 0) { perror("listen"); exit(1); }

    std::cout << "Awaiting " << num_clients << " clients on port " << port << "...\n";

    // 2) 클라이언트 연결 수집
    std::vector<int> socks;
    while ((int)socks.size() < num_clients) {
        int s = accept(listen_fd, nullptr, nullptr);
        if (s < 0) { perror("accept"); continue; }
        std::cout << "Client connected, sock=" << s << "\n";
        socks.push_back(s);
    }

    // 3) epoll 준비
    int epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); exit(1); }

    // 각 소켓을 논블로킹 + epoll에 EPOLLOUT 모드로 등록
    for (int i = 0; i < num_clients; ++i) {
        int fd = socks[i];
        if (setNonBlocking(fd) < 0) {
            std::cerr << "fcntl non-block failed: " << strerror(errno) << "\n";
            exit(1);
        }
        epoll_event ev{};
        ev.events = EPOLLOUT;
        ev.data.u32 = i;  // idx 저장
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            perror("epoll_ctl ADD");
            exit(1);
        }
    }

    std::vector<epoll_event> events(num_clients);
    std::vector<char>    data(data_size, 0), buf(data_size);
    std::vector<size_t>  sent(num_clients), recvd(num_clients);

    // 4) Epoch 루프
    for (int e = 0; e < EPOCHS; ++e) {
        unsigned long epoch_start = timeUs();

        for (int it = 0; it < iterations; ++it) {
            // — Send Phase —
            std::fill(sent.begin(), sent.end(), 0);
            int pending_send = num_clients;

            while (pending_send > 0) {
                int n = epoll_wait(epfd, events.data(), num_clients, -1);
                if (n < 0) { perror("epoll_wait"); exit(1); }

                for (int k = 0; k < n; ++k) {
                    auto &ev = events[k];
                    int idx = ev.data.u32;
                    int fd  = socks[idx];

                    if ((ev.events & EPOLLOUT) && sent[idx] < data_size) {
                        ssize_t m = send(fd, data.data() + sent[idx], data_size - sent[idx], 0);
                        if (m > 0) {
                            sent[idx] += m;
                            if (sent[idx] == (size_t)data_size) {
                                // 전송 완료 → EPOLLIN 모드로 전환
                                epoll_event rev{};
                                rev.events = EPOLLIN;
                                rev.data.u32 = idx;
                                epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &rev);
                                --pending_send;
                            }
                        } else if (m < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                            perror("send");
                            exit(1);
                        }
                    }
                }
            }

            // — Read Phase —
            std::fill(recvd.begin(), recvd.end(), 0);
            int pending_recv = num_clients;

            while (pending_recv > 0) {
                int n = epoll_wait(epfd, events.data(), num_clients, -1);
                if (n < 0) { perror("epoll_wait"); exit(1); }

                for (int k = 0; k < n; ++k) {
                    auto &ev = events[k];
                    int idx = ev.data.u32;
                    int fd  = socks[idx];

                    if ((ev.events & EPOLLIN) && recvd[idx] < data_size) {
                        ssize_t m = read(fd, buf.data() + recvd[idx], data_size - recvd[idx]);
                        if (m > 0) {
                            recvd[idx] += m;
                            if (recvd[idx] == (size_t)data_size) {
                                // 수신 완료 → 다음 iteration 준비를 위해 EPOLLOUT 모드 복원
                                epoll_event sev{};
                                sev.events = EPOLLOUT;
                                sev.data.u32 = idx;
                                epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &sev);
                                --pending_recv;
                            }
                        } else if (m == 0) {
                            std::cerr << "peer closed connection idx=" << idx << "\n";
                            exit(1);
                        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            perror("read");
                            exit(1);
                        }
                    }
                }
            }
            // 한 iteration에 모든 클라이언트의 send/read가 끝나야만 여기로 옵니다 → 동기화 완료
        }

        unsigned long epoch_dur = timeUs() - epoch_start;
        printf("[Epoch %d] avg per iteration = %lu us (%.3f ms)\n",
               e, epoch_dur / iterations, epoch_dur / 1000.0);
    }

    // 5) 정리
    for (int fd : socks) close(fd);
    close(listen_fd);
    close(epfd);
    std::cout << "Server shut down.\n";
    return 0;
}
