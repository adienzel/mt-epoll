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

#include "RingBuffer.h"


#ifndef MT_EPOLL_EPOLL_SERVER_H
#define MT_EPOLL_EPOLL_SERVER_H

constexpr uint16_t PORT = 8088;

constexpr int EVENT_NUM = 20;


static inline int createSocket(uint16_t port);

static inline void accept_request(int k, int socketFd, int eFd, epoll_event &event);

void send_data(const epoll_event *events, int i, char *buffer, size_t len);

void loop_events(int k, int socketFd, int eFd, epoll_event &event, const epoll_event *events, int numOfEvents);

void loop_read_and_send(int eFd, const epoll_event *events, int i, bool running_loop);

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
//    std::cout << "worker : " << k << " accept " << std::endl;
    event.events = EPOLLIN;
    event.data.fd = tfd;
    if (epoll_ctl(eFd, EPOLL_CTL_ADD, tfd, &event) == -1) {
        std::cout << "epoll_ctl add client error: " << getErrorString(errno) << std::endl;
        close(tfd);
        return;
    }
}

void buildSocketInside(uint16_t port, int k) {
    std::cout << " Worker : " << k << " run " << __func__ << std::endl;
    
    auto socketFd = createSocket(port);
    int eFd = epoll_create1(EPOLL_CLOEXEC);
    if (eFd == -1) {
        std::cout << "create epoll fail" << std::endl;
        return;
    }
    
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = socketFd;
    if (epoll_ctl(eFd, EPOLL_CTL_ADD, socketFd, &event) == -1) {
        std::cout << "epoll_ctl add socket error: " << getErrorString(errno) << std::endl;
        close(eFd);
        return;
    }
    epoll_event events[EVENT_NUM];
    
    while (true) {
        auto numOfEvents = epoll_wait(eFd, events, EVENT_NUM, -1);
        if (numOfEvents == -1) {
            std::cout << "epoll error" << std::endl;
            return;
        }
       // std::this_thread::sleep_for((std::chrono::seconds(1)));
        loop_events(k, socketFd, eFd, event, events, numOfEvents);
    }
}

void loop_events(int k, int socketFd, int eFd, epoll_event &event, const epoll_event *events, int numOfEvents) {
    for (int i = 0; i < numOfEvents; ++i) {
        if (events[i].data.fd == socketFd) {
            accept_request(k, socketFd, eFd, event);
        } else {
            // in this mode i don't check data
            
            auto running_loop = true;
            loop_read_and_send(eFd, events, i, running_loop);
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
void loop_read_and_send(int eFd, const epoll_event *events, int i, bool running_loop) {
    thread_local static RingBuffer buffer_pool(8192);
    char buffer[4096];

    struct iovec iov;
    struct msghdr msg = {};
    iov.iov_base = buffer;
    iov.iov_len = sizeof(buffer);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    while (true) {
        auto len = recvmsg(events[i].data.fd, &msg, 0);
        if (len < 0 && errno == EINTR) {
            //std::cout << "EINTR " << events[i].data.fd << std::endl;
            continue;
        }
        if (len < 0) {
            std::cout << "Read error, %s " << strerror(errno) << std::endl;
            running_loop = false;
        } else if (len == 0) {
            std::cout << "EOF Closed connection - descriptor = " << events[i].data.fd << std::endl;
            epoll_ctl(eFd, EPOLL_CTL_DEL, events[i].data.fd, nullptr);
            running_loop = false;
        }
        if (running_loop) {
            send_data(events, i, buffer, len);
        }
        break;
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
    msg.msg_flags |= MSG_ZEROCOPY;  // Enable zero-copy

    len = sendmsg(events[i].data.fd, &msg, MSG_NOSIGNAL);
    if (len == (size_t)-1) {
        std::cout << "error sending data, " << strerror(errno) << std::endl;
    }
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
