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
        struct ifaddrs* ifaddr;
        getifaddrs(&ifaddr);

        for (int i = 0; i < interface_count; ++i) {
            struct in_addr bcast_addr = {0};

            for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                    continue;

                if (strcmp(ifa->ifa_name, interfaces[i]) != 0)
                    continue;

                struct sockaddr_in* bcast = (struct sockaddr_in*)ifa->ifa_broadaddr;
                if (bcast) {
                    bcast_addr = bcast->sin_addr;
                    break;
                }
            }

            if (bcast_addr.s_addr == 0) {
                printf("[WARN] Pas d'adresse de broadcast trouvée pour %s\n", interfaces[i]);
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

            int ret = sendto(sock, router_id, strlen(router_id), 0, (struct sockaddr*)&addr, sizeof(addr));
            if (ret < 0) {
                perror("[ERREUR] sendto");
            } else {
                printf("[DEBUG] Hello envoyé : '%s' via '%s' → %s:%d\n",
                       router_id, interfaces[i],
                       inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
            }

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
        char buf[64];
        struct sockaddr_in sender;
        socklen_t len = sizeof(sender);

        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&sender, &len);
        if (n > 0) {
            buf[n] = '\0';

            char sender_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender.sin_addr, sender_ip, sizeof(sender_ip));

            char clean_id[MAX_NAME_LEN];
            sscanf(buf, "%31s", clean_id);  // nettoyage sécurisé


            if (strcmp(clean_id, router_id) != 0) {
                printf("[DEBUG] Paquet reçu de %s : '%s'\n", sender_ip, clean_id);
                add_or_update_neighbor(clean_id, sender_ip);
            }
            //  else {
            //     printf("[INFO] Paquet ignoré (moi-même)\n");
            // }
        }
    }

    return NULL;
}


void start_hello() {
    pthread_t tx, rx;
    pthread_create(&tx, NULL, hello_sender, NULL);
    pthread_create(&rx, NULL, hello_receiver, NULL);
}
