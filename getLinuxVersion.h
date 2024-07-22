/*
 * Copyright (C) General Motors Company. All rights reserved.
 * This information is confidential and proprietary to GM Company and may not be used, modified, copied or distributed.
 */
//
// Created by PZD9G0 on 8/11/2022.
//


#ifndef MT_EPOLL_GETLINUXVERSION_H
#define MT_EPOLL_GETLINUXVERSION_H

#include <iostream>
#include <tuple>

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cctype>
#include <sys/utsname.h>
#include <tuple>
#include <array>

std::tuple<int, long, long, long, long> getLinuxVersion() {
    struct utsname buffer{};
    
    errno = 0;
    if (uname(&buffer) != 0) {
        perror("uname");
        return std::make_tuple(1, 0, 0, 0, 0);
    }
    auto *p = buffer.release;
    
    std::array<long, 16> ver{};
    int i=0;
    while (*p) {
        if (isdigit(*p)) {
            ver[i] = strtol(p, &p, 10);
            i++;
        } else {
            p++;
        }
    }
    return std::make_tuple(0, ver[0], ver[1], ver[2], ver[3]);
}



#endif //MT_EPOLL_GETLINUXVERSION_H
