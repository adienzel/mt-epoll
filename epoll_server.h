/*
 * Copyright (C) General Motors Company. All rights reserved.
 * This information is confidential and proprietary to GM Company and may not be used, modified, copied or distributed.
 */
//
// Created by PZD9G0 on 8/11/2022.
//

#include <netinet/in.h>
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sys/epoll.h>
#include <thread>
#include <bits/socket.h>
#include <cerrno>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <optional>
#include <unordered_map>
#include <memory>
#include <linux/errqueue.h>

#include "RingBuffer.h"
#include "TLSConfig.h"
#include "SPMCThreadPool.h"


#ifndef MT_EPOLL_EPOLL_SERVER_H
#define MT_EPOLL_EPOLL_SERVER_H

constexpr uint16_t PORT = 8088;
constexpr int EVENT_NUM = 20;
constexpr size_t RING_BUFFER_SIZE = 1024 * 1024;

struct ConnectionContext {
    SSL* ssl = nullptr;
    std::shared_ptr<RingBuffer> input_buffer;
    std::shared_ptr<RingBuffer> output_buffer;
    bool tls_handshake_complete = false;
    uint32_t pending_sends = 0;
};

static std::unordered_map<int, std::unique_ptr<ConnectionContext>> connections;
static std::mutex connections_mutex;


static inline int createSocket(uint16_t port);

static inline void accept_request(int k, int socketFd, int eFd, epoll_event &event);

void send_data(const epoll_event *events, int i, char *buffer, size_t len);
void send_data_zero_copy(int fd, std::shared_ptr<RingBuffer> output_buffer);
bool handle_zero_copy_completion(int fd);

void loop_events(int k, int socketFd, int eFd, epoll_event &event, const epoll_event *events, int numOfEvents, SPMCThreadPool& thread_pool);

void loop_read_and_send(int eFd, const epoll_event *events, int i, bool running_loop, SPMCThreadPool& thread_pool);

bool setNonBlocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) != -1;
}

bool disableNagle(int sockfd) {
    int flag = 1;
    return setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) == 0;
}

static std::string getErrorString(int err) {
    char buf[256];
    return std::string(strerror_r(err, buf, sizeof(buf)));
}

static std::optional<std::string> getClientAddress(sockaddr_in* addr) {
    char ip_str[INET6_ADDRSTRLEN];
    int port;

    if (addr->sin_family == AF_INET) {
        struct sockaddr_in* s = (struct sockaddr_in*)addr;
        inet_ntop(AF_INET, &s->sin_addr, ip_str, sizeof(ip_str));
        port = ntohs(s->sin_port);
        return std::optional<std::string>(std::string(ip_str) + ":" + std::to_string(port));
    } else if (addr->sin_family == AF_INET6) {
        struct sockaddr_in6* s6 = (struct sockaddr_in6*)addr;
        inet_ntop(AF_INET6, &s6->sin6_addr, ip_str, sizeof(ip_str));
        port = ntohs(s6->sin6_port);
        return std::optional<std::string>(std::string(ip_str) + ":" + std::to_string(port));
    } else {
        return std::nullopt;
    }
}

static inline void handle_accept_response(int k, int eFd, epoll_event &event, int tfd) {
    if (tfd <= 0) {
        std::cout << "accept error: " << getErrorString(errno) << std::endl;
        return;
    }

    auto ctx = std::make_unique<ConnectionContext>();
    ctx->input_buffer = std::make_shared<RingBuffer>(RING_BUFFER_SIZE);
    ctx->output_buffer = std::make_shared<RingBuffer>(RING_BUFFER_SIZE);
    
    if (TLSManager::getInstance().isEnabled()) {
        ctx->ssl = TLSManager::getInstance().createSSL();
        if (ctx->ssl) {
            SSL_set_fd(ctx->ssl, tfd);
            SSL_set_accept_state(ctx->ssl);
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        connections[tfd] = std::move(ctx);
    }
    
    event.events = EPOLLIN | EPOLLERR;
    event.data.fd = tfd;
    epoll_ctl(eFd, EPOLL_CTL_ADD, tfd, &event);
}

void buildSocketInside(uint16_t port, int k, const TLSConfig& tls_config) {
    std::cout << " Worker : " << k << " run " << __func__ << std::endl;
    
    auto socketFd = createSocket(port);
    int eFd = epoll_create(1);
    if (eFd == -1) {
        std::cout << "create epoll fail" << std::endl;
        return;
    }

    auto thread_pool = std::make_unique<SPMCThreadPool>(tls_config.worker_threads_per_epoll);
    
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = socketFd;
    epoll_ctl(eFd, EPOLL_CTL_ADD, socketFd, &event);
    epoll_event events[EVENT_NUM];
    
    while (true) {
        auto numOfEvents = epoll_wait(eFd, events, EVENT_NUM, -1);
        if (numOfEvents == -1) {
            std::cout << "epoll error" << std::endl;
            return;
        }
        loop_events(k, socketFd, eFd, event, events, numOfEvents, *thread_pool);
    }
}

void loop_events(int k, int socketFd, int eFd, epoll_event &event, const epoll_event *events, int numOfEvents, SPMCThreadPool& thread_pool) {
    for (int i = 0; i < numOfEvents; ++i) {
        if (events[i].data.fd == socketFd) {
            accept_request(k, socketFd, eFd, event);
        } else {
            if (events[i].events & EPOLLERR) {
                if (handle_zero_copy_completion(events[i].data.fd)) {
                    continue;
                }
            }
            
            auto running_loop = true;
            loop_read_and_send(eFd, events, i, running_loop, thread_pool);
        }
    }
}

/**
 * 
 * @param eFd 
 * @param events 
 * @param i 
 * @param buffer 
 * @param running_loop 
 */
void loop_read_and_send(int eFd, const epoll_event *events, int i, bool running_loop, SPMCThreadPool& thread_pool) {
    int fd = events[i].data.fd;
    std::unique_lock<std::mutex> lock(connections_mutex);
    auto conn_it = connections.find(fd);
    if (conn_it == connections.end()) {
        lock.unlock();
        return;
    }
    
    auto& ctx = conn_it->second;
    char* buffer = ctx->input_buffer->getWriteBuffer(4096);
    if (!buffer) {
        return;
    }

    struct iovec iov;
    struct msghdr msg = {};
    iov.iov_base = buffer;
    iov.iov_len = 4096;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    while (true) {
        ssize_t len;
        
        if (TLSManager::getInstance().isEnabled() && ctx->ssl) {
            if (!ctx->tls_handshake_complete) {
                int ret = SSL_accept(ctx->ssl);
                if (ret <= 0) {
                    int ssl_error = SSL_get_error(ctx->ssl, ret);
                    if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
                        break;
                    } else {
                        running_loop = false;
                        break;
                    }
                }
                ctx->tls_handshake_complete = true;
            }
            
            len = SSL_read(ctx->ssl, buffer, 4096);
            if (len <= 0) {
                int ssl_error = SSL_get_error(ctx->ssl, len);
                if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
                    break;
                } else {
                    running_loop = false;
                    break;
                }
            }
        } else {
            len = recvmsg(fd, &msg, 0);
            if (len < 0 && errno == EINTR) {
                continue;
            }
            if (len < 0) {
                std::cout << "Read error: " << strerror(errno) << std::endl;
                running_loop = false;
                break;
            } else if (len == 0) {
                std::cout << "EOF Closed connection - descriptor = " << fd << std::endl;
                epoll_ctl(eFd, EPOLL_CTL_DEL, fd, nullptr);
                running_loop = false;
                break;
            }
        }
        
        if (running_loop && len > 0) {
            ctx->input_buffer->advanceWrite(len);
            
            WorkItem work_item;
            work_item.client_fd = fd;
            work_item.ssl = ctx->ssl;
            work_item.input_buffer = ctx->input_buffer;
            work_item.output_buffer = ctx->output_buffer;
            work_item.data_len = len;
            work_item.is_tls = TLSManager::getInstance().isEnabled() && ctx->ssl;
            
            thread_pool.enqueue(std::move(work_item));
        }
        break;
    }
    
    if (!running_loop) {
        if (ctx->ssl) {
            SSL_free(ctx->ssl);
        }
        if (lock.owns_lock()) {
            connections.erase(fd);
            lock.unlock();
        } else {
            std::lock_guard<std::mutex> cleanup_lock(connections_mutex);
            connections.erase(fd);
        }
        close(fd);
    } else {
        lock.unlock();
    }
}
/**
 * send_data we can add calls to other workers in this case
 * @param events 
 * @param i 
 * @param buffer 
 * @param len 
 */
void send_data(const epoll_event *events, int i, char *buffer, size_t len) {
    struct msghdr msg = {};
    struct iovec iov = {buffer, len};

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_flags |= MSG_ZEROCOPY;

    len = sendmsg(events[i].data.fd, &msg, MSG_NOSIGNAL);
    if (len == (size_t)-1) {
        std::cout << "error sending data, " << strerror(errno) << std::endl;
    }
}

void send_data_zero_copy(int fd, std::shared_ptr<RingBuffer> output_buffer) {
    struct iovec iov;
    if (!output_buffer->getReadBuffer(&iov, output_buffer->available())) {
        return;
    }
    
    if (iov.iov_len == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(connections_mutex);
    auto conn_it = connections.find(fd);
    if (conn_it == connections.end()) {
        return;
    }
    
    auto& ctx = conn_it->second;
    
    if (TLSManager::getInstance().isEnabled() && ctx->ssl && ctx->tls_handshake_complete) {
        int sent = SSL_write(ctx->ssl, iov.iov_base, iov.iov_len);
        if (sent > 0) {
            output_buffer->advanceRead(sent);
        }
    } else {
        struct msghdr msg = {};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_flags = MSG_ZEROCOPY;

        ssize_t sent = sendmsg(fd, &msg, MSG_NOSIGNAL);
        if (sent > 0) {
            ctx->pending_sends++;
            output_buffer->advanceRead(sent);
        } else if (sent == -1) {
            std::cout << "error sending data: " << strerror(errno) << std::endl;
        }
    }
}

bool handle_zero_copy_completion(int fd) {
    char control[100];
    struct msghdr msg = {};
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    ssize_t ret = recvmsg(fd, &msg, MSG_ERRQUEUE);
    if (ret == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cout << "Error reading error queue: " << strerror(errno) << std::endl;
        }
        return false;
    }

    struct cmsghdr *cm;
    for (cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
        if (cm->cmsg_level == SOL_IP && cm->cmsg_type == IP_RECVERR) {
            struct sock_extended_err *serr = (struct sock_extended_err*)CMSG_DATA(cm);
            if (serr->ee_origin == SO_EE_ORIGIN_ZEROCOPY) {
                std::lock_guard<std::mutex> lock(connections_mutex);
                auto conn_it = connections.find(fd);
                if (conn_it != connections.end()) {
                    conn_it->second->pending_sends--;
                }
                return true;
            }
        }
    }
    return false;
}

/**
 * 
 * @param k 
 * @param socketFd 
 * @param eFd 
 * @param event 
 */
static inline void accept_request(int k, int socketFd, int eFd, epoll_event &event) {
    sockaddr_in cli_addr{};
    socklen_t length = sizeof(cli_addr);
    auto tfd = accept4(socketFd, (sockaddr *) &cli_addr, &length, SOCK_NONBLOCK);
    if (tfd == -1) {
        std::cout << "accept error: " << strerror(errno) << std::endl;
        return;
    }

    // for later use to partialy identify client
    auto client_address = getClientAddress(&cli_addr);
    if (!client_address) {
        std::cout << "Failed to get client address\n";
    }
    if (!disableNagle(tfd)) {
        std::cerr << "Failed to set TCP_NODELAY\n";
        close(tfd);
        return;
    }
    if (!setNonBlocking(tfd)) {
        std::cerr << "Failed to set non-blocking\n";
        close(tfd);
        return;
    }   
    handle_accept_response(k, eFd, event, tfd);
}

/**
 * 
 * @param port 
 * @return 
 */
static inline int createSocket(uint16_t port) {
    auto fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd == -1) {
        std::cout << "create socket error" << std::endl;
        return 0;
    }
    
    if (int on = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const void *) &on, sizeof(on)) < 0) {
        std::cout << "set opt error, ret:" << std::endl;
    }
    if (!disableNagle(fd)) {
        std::cerr << "Failed to set TCP_NODELAY\n";
        close(fd);
        return 0;
    }
    if (!setNonBlocking(fd)) {
        std::cerr << "Failed to set non-blocking\n";
        close(fd);
        return 0;
    }
    struct sockaddr_in6 addr6{};
    addr6.sin6_family = AF_INET6;
    addr6.sin6_addr = in6addr_any; // Equivalent to :: (any address)
    addr6.sin6_port = htons(port);
    
    if (bind(fd, (struct sockaddr*)&addr6, sizeof(addr6)) < 0) {
        std::cout << "bind socket error, port:" << PORT << std::endl;
        close(fd);
        return 0;
    }
    
    if (listen(fd, 100) < 0) {
        std::cout << "listen port error" << std::endl;
        close(fd);
        return 0;
    }
    return fd;
}
#endif //MT_EPOLL_EPOLL_SERVER_H
