#include "hello.h"
#include "config.h"
#include "neighbors.h"
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

static void* hello_sender(void* arg) {
    while (1) {
        for (int i = 0; i < interface_count; ++i) {
            int sock = socket(AF_INET, SOCK_DGRAM, 0);
            if (sock < 0) continue;

            struct sockaddr_in addr = {
                .sin_family = AF_INET,
                .sin_port = htons(HELLO_PORT),
                .sin_addr.s_addr = htonl(INADDR_BROADCAST)
            };

            int yes = 1;
            setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
            setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, interfaces[i], strlen(interfaces[i]));

            sendto(sock, router_id, strlen(router_id), 0, (struct sockaddr*)&addr, sizeof(addr));
            close(sock);
        }
        sleep(HELLO_INTERVAL);
    }
    return NULL;
}

static void* hello_receiver(void* arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) perror("recv socket");

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(HELLO_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));

    while (1) {
        char buf[64];
        struct sockaddr_in sender;
        socklen_t len = sizeof(sender);
        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&sender, &len);
        if (n > 0) {
            buf[n] = '\0';
            if (strcmp(buf, router_id) != 0) {
                add_or_update_neighbor(buf, inet_ntoa(sender.sin_addr));
            }
        }
    }
}

void start_hello() {
    pthread_t tx, rx;
    pthread_create(&tx, NULL, hello_sender, NULL);
    pthread_create(&rx, NULL, hello_receiver, NULL);
}
