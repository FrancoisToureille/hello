#include "hello.h"
#include "config.h"
#include "neighbors.h"
#include "routing.h"
#include "control_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <fcntl.h>

static void* hello_sender(void* arg) {
    while (1) {
        if (is_paused()) {
            sleep(1);
            continue;
        }

        struct ifaddrs* ifaddr;
        getifaddrs(&ifaddr);

        for (int i = 0; i < interface_count; ++i) {
            struct in_addr bcast_addr = {0};

            for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                    continue;
                if (strcmp(ifa->ifa_name, interfaces[i]) != 0)
                    continue;
                if (ifa->ifa_broadaddr) {
                    struct sockaddr_in* bcast = (struct sockaddr_in*)ifa->ifa_broadaddr;
                    bcast_addr = bcast->sin_addr;
                    break;
                }
            }

            if (bcast_addr.s_addr == 0 && strlen(broadcasts[i]) > 0) {
                bcast_addr.s_addr = inet_addr(broadcasts[i]);
            }

            if (bcast_addr.s_addr == 0) {
                continue;
            }

            int sock = socket(AF_INET, SOCK_DGRAM, 0);
            if (sock < 0) continue;

            int yes = 1;
            setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
            setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, interfaces[i], strlen(interfaces[i]) + 1);

            struct sockaddr_in addr = {
                .sin_family = AF_INET,
                .sin_port = htons(HELLO_PORT),
                .sin_addr = bcast_addr
            };

            sendto(sock, router_id, strlen(router_id), 0, (struct sockaddr*)&addr, sizeof(addr));
            close(sock);
        }

        freeifaddrs(ifaddr);
        sleep(HELLO_INTERVAL);
    }
    return NULL;
}

static void* hello_receiver(void* arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("recv socket");
        return NULL;
    }

    // NON BLOQUANT
    fcntl(sock, F_SETFL, O_NONBLOCK);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(HELLO_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return NULL;
    }

    while (1) {
        if (is_paused()) {
            sleep(1);
            continue;
        }

        char buf[64];
        struct sockaddr_in sender;
        socklen_t len = sizeof(sender);

        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&sender, &len);
        if (n > 0) {
            buf[n] = '\0';

            char sender_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender.sin_addr, sender_ip, sizeof(sender_ip));

            char clean_id[MAX_NAME_LEN];
            sscanf(buf, "%31s", clean_id);

            if (strcmp(clean_id, router_id) != 0) {
                add_or_update_neighbor(clean_id, sender_ip);
                send_network_list(sender_ip);
            }
        }

        usleep(100000);  // 100ms pour réduire la charge CPU
    }

    return NULL;
}

void send_network_list(const char* dest_ip) {
    if (is_paused()) return;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("[ROUTING] socket");
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(ROUTING_PORT),
        .sin_addr.s_addr = inet_addr(dest_ip)
    };

    struct ifaddrs* ifaddr;
    getifaddrs(&ifaddr);

    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s|", router_id);

    int first = 1;
    for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET || (ifa->ifa_flags & IFF_LOOPBACK))
            continue;

        struct sockaddr_in* sa = (struct sockaddr_in*)ifa->ifa_addr;
        struct sockaddr_in* netmask = (struct sockaddr_in*)ifa->ifa_netmask;
        if (!netmask) continue;

        uint32_t ip = ntohl(sa->sin_addr.s_addr);
        uint32_t mask = ntohl(netmask->sin_addr.s_addr);
        uint32_t network = ip & mask;

        struct in_addr net_addr = { .s_addr = htonl(network) };

        int cidr = 0;
        for (uint32_t m = mask; m; m <<= 1)
            cidr += (m & 0x80000000) ? 1 : 0;

        char cidr_str[32];
        snprintf(cidr_str, sizeof(cidr_str), "%s/%d:%d", inet_ntoa(net_addr), cidr, 1);

        if (!first)
            strcat(buffer, ",");
        strcat(buffer, cidr_str);
        first = 0;
    }

    freeifaddrs(ifaddr);

    sendto(sock, buffer, strlen(buffer), 0, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
}

void start_hello() {
    pthread_t tx, rx;
    pthread_create(&tx, NULL, hello_sender, NULL);
    pthread_create(&rx, NULL, hello_receiver, NULL);
}
