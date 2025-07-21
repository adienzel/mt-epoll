#ifndef SPMC_THREAD_POOL_H
#define SPMC_THREAD_POOL_H

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <memory>
#include "RingBuffer.h"

// Forward declaration
void send_data_zero_copy(int fd, std::shared_ptr<RingBuffer> output_buffer);

struct WorkItem {
    int client_fd;
    SSL* ssl;
    std::shared_ptr<RingBuffer> input_buffer;
    std::shared_ptr<RingBuffer> output_buffer;
    size_t data_len;
    bool is_tls;
};

class SPMCThreadPool {
public:
    SPMCThreadPool(size_t num_workers) : stop_(false) {
        for (size_t i = 0; i < num_workers; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~SPMCThreadPool() {
        stop();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void enqueue(WorkItem item) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            work_queue_.push(std::move(item));
        }
        condition_.notify_one();
    }

    void stop() {
        stop_.store(true);
        condition_.notify_all();
    }

private:
    void workerLoop() {
        while (!stop_.load()) {
            WorkItem item;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                condition_.wait(lock, [this] { return stop_.load() || !work_queue_.empty(); });
                
                if (stop_.load()) break;
                
                if (!work_queue_.empty()) {
                    item = std::move(work_queue_.front());
                    work_queue_.pop();
                } else {
                    continue;
                }
            }

            processWorkItem(item);
        }
    }

    void processWorkItem(const WorkItem& item) {
        struct iovec iov;
        if (!item.input_buffer->getReadBuffer(&iov, item.data_len)) {
            return;
        }

        char* response_data = item.output_buffer->getWriteBuffer(4096);
        if (!response_data) {
            return;
        }

        const char* http_response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 13\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Hello, World!";

        size_t response_len = strlen(http_response);
        memcpy(response_data, http_response, response_len);
        item.output_buffer->advanceWrite(response_len);
        item.input_buffer->advanceRead(item.data_len);
        
        // Trigger send operation after processing
        send_data_zero_copy(item.client_fd, item.output_buffer);
    }

    std::vector<std::thread> workers_;
    std::queue<WorkItem> work_queue_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
};

#endif
