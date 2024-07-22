#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <cstring>

#include "epoll_server.h"
#include "getLinuxVersion.h"
#include "getInterfaceIndex.h"

__attribute__((noreturn)) void usage() {
    std::cout << "Please give number of threads to run:  \"mt_epoll [number of worker threads]\"" << std::endl;
    exit(0);
}


int main(int argc, const char **argv) {
    if (argc < 2) {
        usage();
    }
    
    auto num_workers = (uint32_t)strtol(argv[1], nullptr, 10);
    if (auto num_cpus = (uint32_t)std::thread::hardware_concurrency(); num_cpus <= num_workers) {
        std::cout << "number of workers : " << num_workers << " grater than number of cores : " << num_cpus << std::endl;
        std::cout << " please set the number to max of nuber of cores - 1" << std::endl;
        exit(0);
    }
    
    auto [err, kernel, major, minor, patch] =  getLinuxVersion();
    if (err != 0) {
        exit(-1);
    }
    std::cout << "Version = " << kernel << "." << major << "." << minor << "." << patch << std::endl;
    
    for (auto const& interface : listHostInterfaces()){
        std::cout << "Interface : " << interface.interface_name << " \t\tindex is : " << getSocketInterfaceIndex(interface.interface_name)
        << " \tFamily type = " << interface.family_name << " \tAddress = " << interface.address_name << std::endl;
    }
    
    
    std::vector<std::thread> threads(num_workers);
    
    for (uint32_t i = 0; i < num_workers; ++i) {
        threads[i] = std::thread(&buildSocketInside, PORT, i + 1);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i, &cpuset);
        if (int rc = pthread_setaffinity_np(threads[i].native_handle(), sizeof(cpu_set_t), &cpuset); rc != 0) {
            std::cout <<  "Error calling pthread_setaffinity_np: " << strerror(errno)  << std::endl;
        }
    }
    
    for (auto &th : threads) {
        th.join();
    }

    return 0;
}
