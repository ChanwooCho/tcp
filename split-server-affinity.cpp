#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <sys/time.h>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
#include <sched.h>

unsigned long timeUs() {
    struct timeval te; 
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

ssize_t read_all(int sock, char* buffer, size_t size, int e, int d) {
    size_t total_read = 0;
    while (total_read < size) {
        ssize_t bytes_read = read(sock, buffer + total_read, size - total_read);
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
    while (total_sent < size) {
        ssize_t bytes_sent = send(sock, data + total_sent, size - total_sent, 0);
        if (bytes_sent < 0) {
            perror("Send error");
            return -1;
        }
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
                if(!cpu_affinity.empty()) {
                    cpu_set_t cpuset;
                    CPU_ZERO(&cpuset);
                    CPU_SET(cpu_affinity[i % cpu_affinity.size()], &cpuset);
                    if(sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == -1) {
                        perror("sched_setaffinity failed");
                    }
                }

                for(;;) {
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
    auto enqueue(F&& f) -> std::future<decltype(f())> {
        using return_type = decltype(f());
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::forward<F>(f)
        );
        
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if(stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks.emplace([task](){ (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker: workers)
            if(worker.joinable()) worker.join();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: server <data_size(KB)> <# of decoders> <# of clients> <port>" << std::endl;
        return -1;
    }

    const int data_size = atoi(argv[1]) * 1024;
    const int iterations = atoi(argv[2]) * 2;
    const int num_clients = atoi(argv[3]);
    const int port = atoi(argv[4]);

    // Configure CPU affinity (adjust based on your device's cores)
    const std::vector<int> cpu_affinity = {0, 1, 2, 3}; // Use 4 cores
    ThreadPool pool(cpu_affinity.size(), cpu_affinity);

    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    std::vector<int> client_sockets;

    char* buffer = new char[data_size];
    char* data = new char[data_size];

    // Socket setup
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "Socket failed" << std::endl;
        delete[] buffer;
        delete[] data;
        return -1;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        delete[] buffer;
        delete[] data;
        return -1;
    }

    // Accept clients
    fd_set read_fds;
    while (client_sockets.size() < num_clients) {
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        
        if (select(server_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            std::cerr << "Select error" << std::endl;
            break;
        }

        if (FD_ISSET(server_fd, &read_fds)) {
            int new_socket;
            sockaddr_in client_addr;
            socklen_t addrlen = sizeof(client_addr);
            if ((new_socket = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen)) >= 0) {
                client_sockets.push_back(new_socket);
            }
        }
    }

    // Main processing loop
    for (int e = 0; e < 50; ++e) {
        unsigned int total_duration = 0;
        
        for (int i = 0; i < iterations; ++i) {
            memset(data, 'A' + i % 26, data_size);
            const auto start_iteration = timeUs();

            std::vector<std::future<void>> send_futures;
            
            // Submit all send tasks
            for (int client_socket : client_sockets) {
                // Submit 7 chunks of 1448 bytes
                for (int j = 0; j < 7; ++j) {
                    send_futures.emplace_back(pool.enqueue(
                        [client_socket, data, e, i] {
                            send_all(client_socket, data, 1448, e, i);
                        }
                    ));
                }
                
                // Submit final 104 byte chunk
                send_futures.emplace_back(pool.enqueue(
                    [client_socket, data, e, i] {
                        send_all(client_socket, data, 104, e, i);
                    }
                ));
            }

            // Wait for all sends
            for (auto& future : send_futures) {
                future.wait();
            }

            // Read responses
            for (int client_socket : client_sockets) {
                read_all(client_socket, buffer, 10240, e, i);
            }

            total_duration += timeUs() - start_iteration;
        }

        std::cout << "Iteration " << e << " completed in "
                  << total_duration / 1000 << " ms" << std::endl;
    }

    // Cleanup
    for (int sock : client_sockets) close(sock);
    close(server_fd);
    delete[] buffer;
    delete[] data;

    return 0;
}
