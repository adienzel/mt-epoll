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

#ifndef MT_EPOLL_EPOLL_SERVER_H
#define MT_EPOLL_EPOLL_SERVER_H

constexpr uint16_t PORT = 8088;

constexpr int EVENT_NUM = 20;


static inline int createSocket(uint16_t port);

static inline void accept_request(int k, int socketFd, int eFd, epoll_event &event);

void send_data(const epoll_event *events, int i, const char *buffer, size_t len);

void loop_events(int k, int socketFd, int eFd, epoll_event &event, const epoll_event *events, int numOfEvents);

void loop_read_and_send(int eFd, const epoll_event *events, int i, bool running_loop);

static inline void handle_accept_response(int k, int eFd, epoll_event &event, int tfd) {
    if (tfd <= 0) {
        std::cout << "accept error: " << strerror(errno) << std::endl;
        return;
    }
//    std::cout << "worker : " << k << " accept " << std::endl;
    event.events = EPOLLIN;
    event.data.fd = tfd;
    epoll_ctl(eFd, EPOLL_CTL_ADD, tfd, &event);
}

void buildSocketInside(uint16_t port, int k) {
    std::cout << " Worker : " << k << " run " << __func__ << std::endl;
    
    auto socketFd = createSocket(port);
    int eFd = epoll_create(1);
    if (eFd == -1) {
        std::cout << "create epoll fail" << std::endl;
        return;
    }
    
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
    char buffer[4096];
    
    while (true) {
        auto len = read(events[i].data.fd, buffer, sizeof(buffer));
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
void send_data(const epoll_event *events, int i, const char *buffer, size_t len) {
    len = send(events[i].data.fd, buffer, len, MSG_NOSIGNAL);
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
    auto tfd = accept(socketFd, (sockaddr *) &cli_addr, &length);
    handle_accept_response(k, eFd, event, tfd);
}

/**
 * 
 * @param port 
 * @return 
 */
static inline int createSocket(uint16_t port) {
    auto fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == -1) {
        std::cout << "create socket error" << std::endl;
        return 0;
    }
    
    if (int on = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const void *) &on, sizeof(on)) < 0) {
        std::cout << "set opt error, ret:" << std::endl;
    }
    
    sockaddr_in sockAddr{};
    sockAddr.sin_port = htons(port);
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_addr.s_addr = htons(INADDR_ANY);
    
    if (bind(fd, (sockaddr *) &sockAddr, sizeof(sockAddr)) < 0) {
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
