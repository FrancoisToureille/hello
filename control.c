#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>

#include "types.h"
#include "control.h"
#include "hello.h"
#include "lsa.h"

void gestion_signal(int sig)
{
    running = 0;
    printf("\nArrêt...\n");

    if (broadcast_sock >= 0)
    {
        close(broadcast_sock);
    }
    if (listen_sock >= 0)
    {
        close(listen_sock);
    }
}

void *thread_ecoute(void *arg)
{
    struct sockaddr_in server_addr, client_addr;
    char buffer[LEN_BUFFER];
    socklen_t client_len = sizeof(client_addr);
    ssize_t bytes_received;
    char hostname[256];
    fd_set readfds;
    struct timeval timeout;

    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    listen_sock = creer_socket_diffusion();
    if (listen_sock < 0)
    {
        pthread_exit(NULL);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(BROADCAST_PORT);

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Erreur bind");
        close(listen_sock);
        pthread_exit(NULL);
    }

    while (running)
    {
        FD_ZERO(&readfds);
        FD_SET(listen_sock, &readfds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int select_result = select(listen_sock + 1, &readfds, NULL, NULL, &timeout);

        if (select_result < 0)
        {
            if (errno == EINTR || !running)
            {
                break;
            }
            perror("Erreur select");
            break;
        }
        else if (select_result == 0)
        {
            continue;
        }

        if (FD_ISSET(listen_sock, &readfds))
        {
            bytes_received = recvfrom(listen_sock, buffer, LEN_BUFFER - 1, 0,
                                      (struct sockaddr *)&client_addr, &client_len);
            if (bytes_received > 0)
            {
                buffer[bytes_received] = '\0';
                if (strncmp(buffer, "HELLO|", 6) == 0)
                {
                    processus_message_hello(buffer, inet_ntoa(client_addr.sin_addr));
                }
                else if (strncmp(buffer, "LSA|", 4) == 0)
                {
                    processus_lsa(buffer, inet_ntoa(client_addr.sin_addr));
                }
                else
                {
                    if (strstr(buffer, hostname) != buffer + 1)
                    {
                        time_t now = time(NULL);
                        char *time_str = ctime(&now);
                        time_str[strlen(time_str) - 1] = '\0';

                        printf("\n [%s] Reçu de %s: %s\n",
                               time_str, inet_ntoa(client_addr.sin_addr), buffer);
                        printf(" Commande: ");
                        fflush(stdout);
                    }
                }
            }
        }
    }

    close(listen_sock);
    listen_sock = -1;
    pthread_exit(NULL);
}


int creer_socket_diffusion()
{
    int sock;
    int broadcast_enable = 1;
    int reuse = 1;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("Erreur création socket");
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
                   &broadcast_enable, sizeof(broadcast_enable)) < 0)
    {
        perror("Erreur setsockopt SO_BROADCAST");
        close(sock);
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) < 0)
    {
        perror("Erreur setsockopt SO_REUSEADDR");
        close(sock);
        return -1;
    }

    return sock;
}

int envoyer_message(const char *message)
{
    struct sockaddr_in broadcast_addr;
    char hostname[256];
    char full_message[LEN_BUFFER];

    if (!running)
        return -1;

    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    snprintf(full_message, sizeof(full_message), "[%s] %s", hostname, message);

    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(BROADCAST_PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr(IP_BROADCAST);

    if (sendto(broadcast_sock, full_message, strlen(full_message), 0,
               (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0) {
        if (running)
        { 
            perror("Erreur sendto");
        }
        return -1;
    }

    printf("Message envoyé: %s\n", message);
    return 0;
}