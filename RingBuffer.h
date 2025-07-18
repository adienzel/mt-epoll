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
            if (len > available_space()) return false;  // Buffer is full

            if (tail_ + len <= size_) {
                memcpy(buffer_ + tail_, data, len);
            } else {
                size_t first_part = size_ - tail_;
                memcpy(buffer_ + tail_, data, first_part);
                memcpy(buffer_, data + first_part, len - first_part);
            }
            tail_ = (tail_ + len) % size_;
            return true;
        }
    
        size_t read(char* out, size_t len) {
            if (head_ == tail_) return 0;  // Buffer is empty

            size_t available = used_space();
            size_t bytesRead = std::min(len, available);
            
            if (head_ + bytesRead <= size_) {
                memcpy(out, buffer_ + head_, bytesRead);
            } else {
                size_t first_part = size_ - head_;
                memcpy(out, buffer_ + head_, first_part);
                memcpy(out + first_part, buffer_, bytesRead - first_part);
            }
            head_ = (head_ + bytesRead) % size_;
            return bytesRead;
        }
    
    private:
        char* buffer_;
        size_t size_, head_, tail_;
        
        size_t used_space() const {
            return (tail_ >= head_) ? (tail_ - head_) : (size_ - head_ + tail_);
        }
        
        size_t available_space() const {
            return size_ - used_space() - 1;  // Keep one slot empty to distinguish full from empty
        }
};

#endif //RING_BUFFER_H
