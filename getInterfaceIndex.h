//
// Created by pzd9g0 on 2/21/23.
//

#include <iostream>
#include <tuple>

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cctype>
#include <sys/utsname.h>
#include <tuple>
#include <array>
#include <cstring>
#include <sys/ioctl.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <vector>
#include <sys/socket.h>
#include <netdb.h>


#ifndef MT_EPOLL_GETINTERFACEINDEX_H
#define MT_EPOLL_GETINTERFACEINDEX_H

[[maybe_unused]] [[nodiscard]] int getSocketInterfaceIndex(const std::string &interfaceName) {
    struct ifreq ifr{};
    size_t if_name_len = interfaceName.length();
    if (if_name_len >= sizeof(ifr.ifr_name)) {
        std::cout << "interface name is too long : " << interfaceName.length() << std::endl;
        return -1;
    }
    
    auto fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::cout << "Failed to open socket, " << strerror(errno) << std::endl;
        return -1;
    }
    
    memcpy(ifr.ifr_name,interfaceName.c_str(),if_name_len);
    ifr.ifr_name[if_name_len] = 0;
    
    if (ioctl(fd,SIOCGIFINDEX, &ifr) < 0) {
        std::cout << "Fail to send 'ioctl', " << strerror(errno) << std::endl;
        return -1;
    }
    return ifr.ifr_ifindex;
}
struct interface_list_t {
    std::string interface_name;
    int family;
    std::string family_name;
    std::string address_name;
};

[[maybe_unused]] [[nodiscard]]inline std::string getFamilyName(int family) {
    std::string str {};
    switch (family) {
        case AF_UNSPEC:
            str = "AF_UNSPEC";
            break;
        //case AF_LOCAL: // like file
        //case AF_UNIX: like file
        case AF_FILE:
            str = "AF_UNIX";
            break;
        case AF_INET:
            str = "AF_INET";
            break;
        case AF_AX25:
            str = "AF_AX25";
            break;
        case AF_IPX:
            str = "AF_IPX";
            break;
        case AF_APPLETALK:
            str = "AF_APPLETALK";
            break;
        case AF_NETROM:
            str = "AF_NETROM";
            break;
        case AF_BRIDGE:
            str = "AF_BRIDGE";
            break;
        case AF_ATMPVC:
            str = "AF_ATMPVC";
            break;
        case AF_X25:
            str = "AF_X25";
            break;
        case AF_INET6:
            str = "AF_INET6";
            break;
        case AF_ROSE:
            str = "AF_ROSE";
            break;
        case AF_DECnet:
            str = "AF_DECnet";
            break;
        case AF_NETBEUI:
            str = "AF_NETBEUI";
            break;
        case AF_SECURITY:
            str = "AF_SECURITY";
            break;
        case AF_KEY:
            str = "AF_KEY";
            break;
//        case AF_NETLINK:
        case AF_ROUTE:
            str = "AF_ROUTE";
            break;
        case AF_PACKET:
            str = "AF_PACKET";
            break;
        case AF_ASH:
            str = "AF_ASH";
            break;
        case AF_ECONET:
            str = "AF_ECONET";
            break;
        case AF_ATMSVC:
            str = "AF_ATMSVC";
            break;
        case AF_RDS:
            str = "AF_RDS";
            break;
        case AF_SNA:
            str = "AF_SNA";
            break;
        case AF_IRDA:
            str = "AF_IRDA";
            break;
        case AF_PPPOX:
            str = "AF_PPPOX";
            break;
        case AF_WANPIPE:
            str = "AF_WANPIPE";
            break;
        case AF_LLC:
            str = "AF_LLC";
            break;
        case AF_IB:
            str = "AF_IB";
            break;
        case AF_MPLS:
            str = "AF_MPLS";
            break;
        case AF_CAN:
            str = "AF_CAN";
            break;
        case AF_TIPC:
            str = "AF_TIPC";
            break;
        case AF_BLUETOOTH:
            str = "AF_BLUETOOTH";
            break;
        case AF_IUCV:
            str = "AF_IUCV";
            break;
        case AF_RXRPC:
            str = "AF_RXRPC";
            break;
        case AF_ISDN:
            str = "AF_ISDN";
            break;
        case AF_PHONET:
            str = "AF_PHONET";
            break;
        case AF_IEEE802154:
            str = "AF_IEEE802154";
            break;
        case AF_CAIF:
            str = "AF_CAIF";
            break;
        case AF_ALG:
            str = "AF_ALG";
            break;
        case AF_NFC:
            str = "AF_NFC";
            break;
        case AF_VSOCK:
            str = "AF_VSOCK";
            break;
        case AF_KCM:
            str = "AF_KCM";
            break;
        case AF_QIPCRTR:
            str = "AF_QIPCRTR";
            break;
        case AF_SMC:
            str = "AF_SMC";
            break;
        case AF_XDP:
            str = "AF_XDP";
            break;
        //case AF_MAX:
        default:
            str = "Undefined";
            break;
            
    }
    return str;
}

[[maybe_unused]] [[nodiscard]] inline std::vector<interface_list_t> listHostInterfaces() {
    struct ifaddrs *addresses;
    std::vector<interface_list_t> interface_list {};
    
    if (getifaddrs(&addresses) < 0) {
        std::cout << "Fail to request 'getifaddrs', " << strerror(errno) << std::endl;
        return interface_list;
    }
    
    struct ifaddrs *address = addresses;
    while (address) {
        interface_list_t interface {};
        interface.interface_name = address->ifa_name;
        interface.family = address->ifa_addr->sa_family;
        interface.family_name = getFamilyName(interface.family);
        
        if (interface.family == AF_INET || interface.family == AF_INET6) {
            //std::cout << "interface name = " <<  address->ifa_name << "\t";
            std::array<char, 128> ap {};
            //char ap[100];
            auto family_size = (address->ifa_addr->sa_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);   
            getnameinfo(address->ifa_addr,family_size, ap.data(), ap.size(), 0, 0, NI_NUMERICHOST);
            interface.address_name = ap.data();
            //std::cout << "\t address name = " <<  ap.data() << std::endl;
        }
        interface_list.emplace_back(interface);
        address = address->ifa_next;
    }
    freeifaddrs(addresses);
    return interface_list;
}
#endif //MT_EPOLL_GETINTERFACEINDEX_H
