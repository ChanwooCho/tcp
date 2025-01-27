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

unsigned long timeUs() {
    struct timeval te; 
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000000LL + te.tv_usec;
}

unsigned int min_latency;

// Thread pool structure
struct ThreadInfo {
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop = false;
    int cpu_id;
};

struct ThreadPool {
    std::vector<std::thread> workers;
    std::vector<ThreadInfo> thread_info;
    std::atomic<int> task_count{0};
    std::mutex completion_mutex;
    std::condition_variable completion_cv;

    ThreadPool(int num_threads) {
        thread_info.resize(num_threads);
        for(int i = 0; i < num_threads; ++i) {
            thread_info[i].cpu_id = i;
            workers.emplace_back([this, i] { worker_thread(i); });
        }
    }

    ~ThreadPool() {
        for(auto& info : thread_info) {
            {
                std::unique_lock<std::mutex> lock(info.queue_mutex);
                info.stop = true;
            }
            info.condition.notify_all();
        }
        for(auto& worker : workers) {
            if(worker.joinable()) worker.join();
        }
    }

    void worker_thread(int thread_idx) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(thread_info[thread_idx].cpu_id, &cpuset);
        if(sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == -1) {
            perror("sched_setaffinity failed");
        }

        while(true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(thread_info[thread_idx].queue_mutex);
                thread_info[thread_idx].condition.wait(lock, [&] {
                    return !thread_info[thread_idx].tasks.empty() || thread_info[thread_idx].stop;
                });

                if(thread_info[thread_idx].stop) return;

                task = std::move(thread_info[thread_idx].tasks.front());
                thread_info[thread_idx].tasks.pop();
            }

            task();
            
            if(--task_count == 0) {
                completion_cv.notify_one();
            }
        }
    }

    template<class F>
    void enqueue(F&& f, int cpu_id) {
        auto& info = thread_info[cpu_id];
        {
            std::unique_lock<std::mutex> lock(info.queue_mutex);
            info.tasks.emplace(std::forward<F>(f));
        }
        ++task_count;
        info.condition.notify_one();
    }

    void wait_completion() {
        std::unique_lock<std::mutex> lock(completion_mutex);
        completion_cv.wait(lock, [this] { return task_count == 0; });
    }
};

// Rest of your existing functions (read_all, send_all) remain the same
// [Keep the original read_all and send_all implementations unchanged]

int main(int argc, char *argv[]) {
    // [Keep the original main setup code unchanged until after connect()]

    // Create thread pool with 4 workers (one per CPU 0-3)
    ThreadPool pool(4);

    unsigned int before1;
    unsigned int interval1;
    for (int e = 0; e < 50; ++e) {
        for (int i = 0; i < iterations; ++i) {
            memset(data, 'A' + i % 26, data_size);
            before1 = timeUs();
            
            read_all(sock, buffer, 10240, e, i);

            // Submit all send tasks to the thread pool
            for (int j = 0; j < 8; ++j) {
                size_t send_size = (j < 7) ? 1448 * 1 : 104;
                int target_cpu = j % 4;
                pool.enqueue([=] {
                    send_all(sock, data, send_size, e, i);
                }, target_cpu);
            }

            // Wait for all tasks to complete
            pool.wait_completion();

            interval1 = timeUs() - before1;
            printf("iteration %d decoder %d: interval = %dus\n", e, i, interval1);
            printf("==============================================================\n");
        }
    }

    // [Rest of cleanup code remains unchanged]
}
