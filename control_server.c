// control_server.c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <stdlib.h>
#include "control_server.h"

static volatile int paused = 0;

int is_paused() {
    return paused;
}

void* control_server_thread(void* arg) {
    int server_fd, client_fd;
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(CONTROL_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);

    printf("[CTRL] Serveur de contrôle en écoute sur le port %d\n", CONTROL_PORT);

    while (1) {
        client_fd = accept(server_fd, NULL, NULL);
        char buffer[64] = {0};
        read(client_fd, buffer, sizeof(buffer)-1);

        if (strncmp(buffer, "pause", 5) == 0) {
            paused = 1;
            printf("[CTRL] Mise en pause du routeur\n");
        } else if (strncmp(buffer, "resume", 6) == 0) {
            paused = 0;
            printf("[CTRL] Reprise du routeur\n");
        }

        close(client_fd);
    }

    return NULL;
}
