#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
#include <sched.h>
#include <vector>

unsigned long timeUs() {
    struct timeval te; 
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

unsigned int min_latency = UINT_MAX;
std::mutex latency_mutex;

ssize_t read_all(int sock, char* buffer, size_t size, int e, int d) {
    size_t total_read = 0;
    unsigned int is_first = 1;
    
    while (total_read < size) {
        unsigned int before = timeUs();
        ssize_t bytes_read = read(sock, buffer + total_read, size - total_read);
        unsigned int interval = timeUs() - before;

        {
            std::lock_guard<std::mutex> lock(latency_mutex);
            if (is_first && min_latency > interval) {
                min_latency = interval;
            }
        }
        
        is_first = 0;
        printf("iteration %d decoder %d: bytes_read = %d, interval = %dus\n", 
              e, d, bytes_read, interval);

        if (bytes_read < 0) return -1;
        if (bytes_read == 0) break;
        total_read += bytes_read;
    }
    return total_read;
}

ssize_t send_all(int sock, const char* data, size_t size, int e, int d) {
    size_t total_sent = 0;
    while (total_sent < size) {
        ssize_t bytes_sent = send(sock, data + total_sent, size - total_sent, 0);
        if (bytes_sent < 0) return -1;
        total_sent += bytes_sent;
    }
    return total_sent;
}

class ThreadPool {
public:
    ThreadPool(size_t threads, const std::vector<int>& cpu_affinity) 
        : stop(false) {
        for(size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this, i, cpu_affinity] {
                // Set CPU affinity
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cpu_affinity[i % cpu_affinity.size()], &cpuset);
                sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

                while(true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this]{ return stop || !tasks.empty(); });
                        if(stop && tasks.empty()) return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker : workers)
            if(worker.joinable()) worker.join();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

struct ClientSession {
    int sock;
    char* buffer;
    char* data;
    int data_size;
    int iterations;
    int decoder_id;
};

void process_session(ClientSession& session) {
    for (int e = 0; e < 50; ++e) {
        for (int i = 0; i < session.iterations; ++i) {
            memset(session.data, 'A' + i % 26, session.data_size);
            unsigned int before = timeUs();

            read_all(session.sock, session.buffer, 10240, e, session.decoder_id);

            for (int j = 0; j < 7; j++) {
                send_all(session.sock, session.data, 1448, e, session.decoder_id);
            }
            send_all(session.sock, session.data, 104, e, session.decoder_id);

            unsigned int interval = timeUs() - before;
            printf("Decoder %d iteration %d: total interval = %dus\n", 
                  session.decoder_id, i, interval);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: client <data_size(KB)> <# of decoders> <ip_address:port>" << std::endl;
        return -1;
    }

    const int data_size = std::atoi(argv[1]) * 1024;
    const int num_decoders = std::atoi(argv[2]);
    const std::string address(argv[3]);

    // Parse IP and port
    size_t colon_pos = address.find(':');
    if (colon_pos == std::string::npos) {
        std::cerr << "Invalid address format" << std::endl;
        return -1;
    }
    std::string ip = address.substr(0, colon_pos);
    int port = std::atoi(address.substr(colon_pos+1).c_str());

    // Configure CPU affinity
    const std::vector<int> cpu_affinity = {0, 1, 2, 3};
    ThreadPool pool(cpu_affinity.size(), cpu_affinity);

    // Create multiple client sessions
    std::vector<ClientSession> sessions;
    std::vector<std::future<void>> futures;

    for(int d = 0; d < num_decoders; ++d) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if(sock < 0) {
            std::cerr << "Socket creation failed for decoder " << d << std::endl;
            continue;
        }

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        
        if(inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
            std::cerr << "Invalid address for decoder " << d << std::endl;
            close(sock);
            continue;
        }

        if(connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            std::cerr << "Connection failed for decoder " << d << std::endl;
            close(sock);
            continue;
        }

        sessions.push_back({
            sock,
            new char[data_size],
            new char[data_size],
            data_size,
            num_decoders * 2,  // iterations
            d                  // decoder_id
        });
    }

    // Submit tasks for each session
    for(auto& session : sessions) {
        futures.emplace_back(pool.enqueue([&session] {
            process_session(session);
        }));
    }

    // Wait for all tasks to complete
    for(auto& future : futures) {
        future.wait();
    }

    // Cleanup
    for(auto& session : sessions) {
        close(session.sock);
        delete[] session.buffer;
        delete[] session.data;
    }

    std::cout << "Minimum latency observed: " << min_latency << "us" << std::endl;
    return 0;
}
