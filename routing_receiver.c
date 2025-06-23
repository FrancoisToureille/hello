#include "routing_receiver.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_ROUTES 128

typedef struct {
    char network[32];   // e.g., "10.1.0.0/24"
    int distance;       // e.g., 1, 2, ...
    char via[MAX_NAME_LEN]; // qui a fourni l'info
} Route;

static Route routing_table[MAX_ROUTES];
static int route_count = 0;
static pthread_mutex_t routing_mutex = PTHREAD_MUTEX_INITIALIZER;

static int route_exists(const char* network, const char* via) {
    for (int i = 0; i < route_count; ++i) {
        if (strcmp(routing_table[i].network, network) == 0 &&
            strcmp(routing_table[i].via, via) == 0) {
            return 1;
        }
    }
    return 0;
}

void* routing_receiver_thread(void* arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("[ROUTING] socket");
        return NULL;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(ROUTING_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[ROUTING] bind");
        close(sock);
        return NULL;
    }

    char buf[512];
    struct sockaddr_in sender;
    socklen_t len = sizeof(sender);

    while (1) {
        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&sender, &len);
        if (n <= 0) continue;

        buf[n] = '\0';
        char sender_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender.sin_addr, sender_ip, sizeof(sender_ip));

        char* pipe = strchr(buf, '|');
        if (!pipe) continue;

        *pipe = '\0';
        char* sender_id = buf;
        char* networks_str = pipe + 1;

        pthread_mutex_lock(&routing_mutex);

        char* token = strtok(networks_str, ",");
        while (token) {
            char network[32];
            int distance;

            if (sscanf(token, "%31[^:]:%d", network, &distance) == 2) {
                if (!route_exists(network, sender_id)) {
                    if (route_count < MAX_ROUTES) {
                        strncpy(routing_table[route_count].network, network, sizeof(routing_table[route_count].network));
                        routing_table[route_count].distance = distance;
                        strncpy(routing_table[route_count].via, sender_id, sizeof(routing_table[route_count].via));
                        route_count++;
                        printf("[ROUTING] Nouvelle route reçue de %s : %s (distance %d)\n", sender_id, network, distance);
                    }
                }
            }

            token = strtok(NULL, ",");
        }

        pthread_mutex_unlock(&routing_mutex);
    }

    close(sock);
    return NULL;
}

void print_routing_table() {
    pthread_mutex_lock(&routing_mutex);
    printf("\n--- Table de routage (simulée) ---\n");
    for (int i = 0; i < route_count; ++i) {
        printf("  %s via %s (distance %d)\n",
               routing_table[i].network,
               routing_table[i].via,
               routing_table[i].distance);
    }
    printf("----------------------------------\n");
    pthread_mutex_unlock(&routing_mutex);
}
