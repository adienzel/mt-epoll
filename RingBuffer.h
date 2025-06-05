#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <cstddef>
#include <cstring>
#include <algorithm>

class RingBuffer {
    public:
        RingBuffer(size_t size) : size_(size), head_(0), tail_(0) {
            buffer_ = new char[size_];
        }
    
        ~RingBuffer() { delete[] buffer_; }
    
        bool write(const char* data, size_t len) {
            if ((tail_ + len) % size_ == head_) return false;  // Buffer is full
    
            memcpy(buffer_ + tail_, data, len);
            tail_ = (tail_ + len) % size_;
            return true;
        }
    
        size_t read(char* out, size_t len) {
            if (head_ == tail_) return 0;  // Buffer is empty
    
            size_t bytesRead = std::min(len, tail_ - head_);
            memcpy(out, buffer_ + head_, bytesRead);
            head_ = (head_ + bytesRead) % size_;
            return bytesRead;
        }
    
    private:
        char* buffer_;
        size_t size_, head_, tail_;
};

#endif //RING_BUFFER_H
