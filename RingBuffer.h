#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <cstddef>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <sys/uio.h>

class RingBuffer {
    public:
        RingBuffer(size_t size) : size_(size), head_(0), tail_(0) {
            buffer_ = new char[size_];
        }
    
        ~RingBuffer() { delete[] buffer_; }
    
        bool write(const char* data, size_t len) {
            if ((tail_ + len) % size_ == head_) return false;
            
            memcpy(buffer_ + tail_, data, len);
            tail_ = (tail_ + len) % size_;
            return true;
        }
    
        size_t read(char* out, size_t len) {
            if (head_ == tail_) return 0;
    
            size_t bytesRead = std::min(len, tail_ - head_);
            memcpy(out, buffer_ + head_, bytesRead);
            head_ = (head_ + bytesRead) % size_;
            return bytesRead;
        }

        bool writeZeroCopy(const struct iovec* iov, int iovcnt) {
            size_t total_len = 0;
            for (int i = 0; i < iovcnt; ++i) {
                total_len += iov[i].iov_len;
            }
            
            if ((tail_ + total_len) % size_ == head_) return false;
            
            for (int i = 0; i < iovcnt; ++i) {
                memcpy(buffer_ + tail_, iov[i].iov_base, iov[i].iov_len);
                tail_ = (tail_ + iov[i].iov_len) % size_;
            }
            return true;
        }

        bool getReadBuffer(struct iovec* iov, size_t max_len) {
            if (head_ == tail_) return false;
            
            size_t available = (tail_ >= head_) ? (tail_ - head_) : (size_ - head_);
            size_t to_read = std::min(available, max_len);
            
            iov->iov_base = buffer_ + head_;
            iov->iov_len = to_read;
            return true;
        }

        void advanceRead(size_t len) {
            head_ = (head_ + len) % size_;
        }

        char* getWriteBuffer(size_t len) {
            if ((tail_ + len) % size_ == head_) return nullptr;
            return buffer_ + tail_;
        }

        void advanceWrite(size_t len) {
            tail_ = (tail_ + len) % size_;
        }

        size_t available() const {
            return (tail_ >= head_) ? (tail_ - head_) : (size_ - head_ + tail_);
        }

        size_t capacity() const { return size_; }
    
    private:
        char* buffer_;
        size_t size_;
        std::atomic<size_t> head_;
        std::atomic<size_t> tail_;
};

#endif
