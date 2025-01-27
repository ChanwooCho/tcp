#define _GNU_SOURCE
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <sched.h>
#include <sys/time.h>

// Utility function for microsecond timestamps
unsigned long timeUs() {
    struct timeval te; 
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

unsigned int min_latency = UINT_MAX;

// Network I/O functions
ssize_t read_all(int sock, char* buffer, size_t size, int e, int d) {
    size_t total_read = 0;
    unsigned int before;
    unsigned int interval;
    unsigned int is_first = 1;
    
    while (total_read < size) {
        before = timeUs();
        ssize_t bytes_read = read(sock, buffer + total_read, size - total_read);
        interval = timeUs() - before;
        
        if (is_first && min_latency > interval) {
            min_latency = interval;
        }
        is_first = 0;
        
        printf("READ  [iter %d][dec %d]: %zd bytes, %uus\n", e, d, bytes_read, interval);

        if (bytes_read < 0) {
            perror("Read error");
            return -1;
        } else if (bytes_read == 0) {
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
        
        printf("SEND  [iter %d][dec %d]: %zd bytes, %uus\n", e, d, bytes_sent, interval);

        if (bytes_sent < 0) {
            perror("Send error");
            return -1;
        }
        total_sent += bytes_sent;
    }
    return total_sent;
}

// Thread pool implementation
class ThreadPool {
public:
    ThreadPool(size_t num_threads, const std::vector<int>& cpu_affinity) 
        : stop(false) {
        if(num_threads != cpu_affinity.size()) {
            throw std::runtime_error("CPU affinity list must match thread count");
        }

        for(size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this, i, cpu_affinity] {
                // Set CPU affinity
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cpu_affinity[i], &cpuset);
                if(sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == -1) {
                    perror("sched_setaffinity failed");
                }

                // Worker loop
                while(true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { 
                            return stop || !tasks.empty(); 
                        });

                        if(stop && tasks.empty()) return;

                        task = std::move(tasks.front());
                        tasks.pop();
                    }

                    task();
                    {
                        std::lock_guard<std::mutex> lock(completion_mutex);
                        --outstanding_tasks;
                        if(outstanding_tasks == 0) {
                            completion_condition.notify_all();
                        }
                    }
                }
            });
        }
    }

    template<class F>
    void enqueue(F&& f) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            tasks.emplace(std::forward<F>(f));
            ++outstanding_tasks;
        }
        condition.notify_one();
    }

    void wait_completion() {
        std::unique_lock<std::mutex> lock(completion_mutex);
        completion_condition.wait(lock, [this] {
            return outstanding_tasks == 0;
        });
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker : workers) {
            worker.join();
        }
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    
    std::mutex queue_mutex;
    std::mutex completion_mutex;
    
    std::condition_variable condition;
    std::condition_variable completion_condition;
    
    std::atomic<uint32_t> outstanding_tasks{0};
    bool stop;
};

int main(int argc, char *argv[]) {
    if(argc != 4) {
        std::cerr << "Usage: " << argv[0] 
                  << " <data_size_KB> <num_decoders> <ip:port>\n";
        return 1;
    }

    // Configuration
    const int data_size = std::atoi(argv[1]) * 1024;
    const int iterations = std::atoi(argv[2]) * 2;
    const std::string ip_port(argv[3]);

    // Parse IP/port
    size_t colon = ip_port.find(':');
    if(colon == std::string::npos) {
        std::cerr << "Invalid IP:port format\n";
        return 1;
    }
    std::string ip = ip_port.substr(0, colon);
    int port = std::stoi(ip_port.substr(colon+1));

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // Connect to server
    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if(inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address\n";
        close(sock);
        return 1;
    }

    if(connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return 1;
    }

    std::cout << "Connected to " << ip << ":" << port << std::endl;

    // Create thread pool pinned to CPUs 0-3
    std::vector<int> cpu_affinity = {0, 1, 2, 3};
    ThreadPool pool(4, cpu_affinity);

    // Buffers
    char* buffer = new char[data_size];
    char* data = new char[data_size];

    // Main loop
    for(int epoch = 0; epoch < 50; ++epoch) {
        for(int dec = 0; dec < iterations; ++dec) {
            const unsigned start_time = timeUs();
            
            // Generate test data
            memset(data, 'A' + dec % 26, data_size);

            // Read phase
            read_all(sock, buffer, 10240, epoch, dec);

            // Parallel send phase
            for(int j = 0; j < 8; ++j) {
                const size_t send_size = (j < 7) ? 1448 : 104;
                pool.enqueue([=] {
                    send_all(sock, data, send_size, epoch, dec);
                });
            }

            // Wait for all sends
            pool.wait_completion();

            // Statistics
            const unsigned elapsed = timeUs() - start_time;
            std::cout << "Epoch " << epoch << " Decoder " << dec 
                      << " completed in " << elapsed << "μs\n"
                      << std::string(60, '-') << "\n";
        }
    }

    // Cleanup
    close(sock);
    delete[] buffer;
    delete[] data;

    return 0;
}
