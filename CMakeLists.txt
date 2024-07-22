cmake_minimum_required(VERSION 3.20)
project(mt_epoll)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CAMKE_CXX_FLAGS} -pthread -Wall -Wpedantic")

add_executable(mt_epoll main.cpp epoll_server.h getLinuxVersion.h getInterfaceIndex.h)
