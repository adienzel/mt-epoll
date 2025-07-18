#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <cstring>
#include <fstream>

#include "epoll_server.h"
#include "getLinuxVersion.h"
#include "getInterfaceIndex.h"
#include "TLSConfig.h"

__attribute__((noreturn)) void usage() {
    std::cout << "Usage: mt_epoll [number of epoll threads] [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --tls                    Enable TLS" << std::endl;
    std::cout << "  --mtls                   Enable mutual TLS" << std::endl;
    std::cout << "  --cert <file>            Certificate file" << std::endl;
    std::cout << "  --key <file>             Private key file" << std::endl;
    std::cout << "  --ca <file>              CA certificate file" << std::endl;
    std::cout << "  --workers-per-epoll <n>  Worker threads per epoll thread (default: 4)" << std::endl;
    std::cout << "  --config <file>          Configuration file" << std::endl;
    exit(0);
}

TLSConfig parseConfig(const std::string& config_file) {
    TLSConfig config;
    std::ifstream file(config_file);
    if (!file.is_open()) {
        return config;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("tls_enabled=") == 0) {
            config.enabled = (line.substr(12) == "true");
        } else if (line.find("mutual_tls=") == 0) {
            config.mutual_tls = (line.substr(11) == "true");
        } else if (line.find("cert_file=") == 0) {
            config.cert_file = line.substr(10);
        } else if (line.find("key_file=") == 0) {
            config.key_file = line.substr(9);
        } else if (line.find("ca_file=") == 0) {
            config.ca_file = line.substr(8);
        } else if (line.find("worker_threads_per_epoll=") == 0) {
            config.worker_threads_per_epoll = std::stoi(line.substr(25));
        }
    }
    return config;
}


int main(int argc, const char **argv) {
    if (argc < 2) {
        usage();
    }
    
    auto num_workers = (uint32_t)strtol(argv[1], nullptr, 10);
    if (auto num_cpus = (uint32_t)std::thread::hardware_concurrency(); num_cpus <= num_workers) {
        std::cout << "number of workers : " << num_workers << " greater than number of cores : " << num_cpus << std::endl;
        std::cout << " please set the number to max of number of cores - 1" << std::endl;
        exit(0);
    }

    TLSConfig tls_config;
    
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tls") {
            tls_config.enabled = true;
        } else if (arg == "--mtls") {
            tls_config.mutual_tls = true;
        } else if (arg == "--cert" && i + 1 < argc) {
            tls_config.cert_file = argv[++i];
        } else if (arg == "--key" && i + 1 < argc) {
            tls_config.key_file = argv[++i];
        } else if (arg == "--ca" && i + 1 < argc) {
            tls_config.ca_file = argv[++i];
        } else if (arg == "--workers-per-epoll" && i + 1 < argc) {
            tls_config.worker_threads_per_epoll = std::stoi(argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            tls_config = parseConfig(argv[++i]);
        }
    }

    if (!TLSManager::getInstance().initialize(tls_config)) {
        std::cerr << "Failed to initialize TLS" << std::endl;
        exit(-1);
    }
    
    auto [err, kernel, major, minor, patch] = getLinuxVersion();
    if (err != 0) {
        exit(-1);
    }
    std::cout << "Version = " << kernel << "." << major << "." << minor << "." << patch << std::endl;
    
    for (auto const& interface : listHostInterfaces()) {
        std::cout << "Interface : " << interface.interface_name << " \t\tindex is : " << getSocketInterfaceIndex(interface.interface_name)
        << " \tFamily type = " << interface.family_name << " \tAddress = " << interface.address_name << std::endl;
    }
    
    std::cout << "TLS enabled: " << (tls_config.enabled ? "yes" : "no") << std::endl;
    std::cout << "Worker threads per epoll: " << tls_config.worker_threads_per_epoll << std::endl;
    
    std::vector<std::thread> threads(num_workers);
    
    for (uint32_t i = 0; i < num_workers; ++i) {
        threads[i] = std::thread(&buildSocketInside, PORT, i + 1, std::ref(tls_config));
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i, &cpuset);
        if (int rc = pthread_setaffinity_np(threads[i].native_handle(), sizeof(cpu_set_t), &cpuset); rc != 0) {
            std::cout << "Error calling pthread_setaffinity_np: " << strerror(errno) << std::endl;
        }
    }
    
    for (auto &th : threads) {
        th.join();
    }

    return 0;
}
