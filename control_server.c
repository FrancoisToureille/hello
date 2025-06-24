#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "control_server.h"
#include "config.h"

static volatile int paused = 0;

int is_paused() {
    return paused;
}

void* control_server_thread(void* arg) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("control_server socket");
        pthread_exit(NULL);
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(CONTROL_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("control_server bind");
        close(server_fd);
        pthread_exit(NULL);
    }

    if (listen(server_fd, 1) < 0) {
        perror("control_server listen");
        close(server_fd);
        pthread_exit(NULL);
    }

    printf("[CTRL] Serveur de contrôle en écoute sur le port %d\n", CONTROL_PORT);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("control_server accept");
            continue;
        }

        char buffer[64] = {0};
        ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';

            if (strncmp(buffer, "pause", 5) == 0) {
                paused = 1;
                printf("[CTRL] Mise en pause du routeur\n");
            } else if (strncmp(buffer, "resume", 6) == 0) {
                paused = 0;
                printf("[CTRL] Reprise du routeur\n");
            } else {
                printf("[CTRL] Commande inconnue : %s\n", buffer);
            }
        }

        close(client_fd);
    }

    close(server_fd);
    return NULL;
}
