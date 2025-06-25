#include "types.h"
#include "control.h"
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <sys/select.h>

#define BROADCAST_IP "255.255.255.255"  // Définition manquante dans types.h
#define BUFFER_SIZE TAILLE_BUFFER        // Pour cohérence avec types.h
#define BROADCAST_PORT PORT_DIFFUSION    // Pour cohérence avec types.h
#define MAX_INTERFACES NB_MAX_INTERFACES // Pour cohérence

// Variables globales externes à définir dans un autre fichier .c
extern volatile int en_fonctionnement;
extern int socket_diffusion;
extern int socket_ecoute;

extern interface_reseau_t interfaces[NB_MAX_INTERFACES];
extern int nombre_interfaces;

extern pthread_mutex_t mutex_voisins;
extern pthread_mutex_t mutex_topologie;
extern pthread_mutex_t mutex_routage;

extern pthread_mutex_t mutex_voisins;

void gestion_signal(int sig)
{
    en_fonctionnement = 0;
    printf("\nArrêt en cours..\n");

    if (socket_diffusion >= 0)
    {
        close(socket_diffusion);
    }
    if (socket_ecoute >= 0)
    {
        close(socket_ecoute);
    }
}

// Initialisation des mutex
void lock_all_mutexes()
{
    pthread_mutex_lock(&mutex_voisins);
    pthread_mutex_lock(&mutex_topologie);
    pthread_mutex_lock(&mutex_routage);
}

void unlock_all_mutexes()
{
    pthread_mutex_unlock(&mutex_routage);
    pthread_mutex_unlock(&mutex_topologie);
    pthread_mutex_unlock(&mutex_voisins);
}

void *listen_thread(void *arg)
{
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    socklen_t client_len = sizeof(client_addr);
    ssize_t bytes_received;
    char hostname[256];
    fd_set readfds;
    struct timeval timeout;

    socket_ecoute = create_broadcast_socket();
    if (socket_ecoute < 0)
    {
        pthread_exit(NULL);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(BROADCAST_PORT);

    if (bind(socket_ecoute, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Erreur bind");
        close(socket_ecoute);
        pthread_exit(NULL);
    }

    printf("🔊 Écoute active sur le port %d\n", BROADCAST_PORT);

    while (en_fonctionnement)
    {
        FD_ZERO(&readfds);
        FD_SET(socket_ecoute, &readfds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int select_result = select(socket_ecoute + 1, &readfds, NULL, NULL, &timeout);

        if (select_result < 0)
        {
            if (errno == EINTR || !en_fonctionnement)
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

        if (FD_ISSET(socket_ecoute, &readfds))
        {
            bytes_received = recvfrom(socket_ecoute, buffer, BUFFER_SIZE - 1, 0,
                                      (struct sockaddr *)&client_addr, &client_len);

            if (bytes_received > 0)
            {
                buffer[bytes_received] = '\0';

                // Déterminer le type de message
                if (strncmp(buffer, "HELLO|", 6) == 0)
                {
                    process_hello_message(buffer, inet_ntoa(client_addr.sin_addr));
                }
                else if (strncmp(buffer, "LSA|", 4) == 0)
                {
                    process_lsa_message(buffer, inet_ntoa(client_addr.sin_addr));
                }
                else
                {
                    if (strstr(buffer, hostname) != buffer + 1)
                    {
                        time_t now = time(NULL);
                        char *time_str = ctime(&now);
                        time_str[strlen(time_str) - 1] = '\0';

                        printf("\n📨 [%s] Reçu de %s: %s\n",
                               time_str, inet_ntoa(client_addr.sin_addr), buffer);
                        printf("💬 Commande: ");
                        fflush(stdout);
                    }
                }
            }
        }
    }

    close(socket_ecoute);
    socket_ecoute = -1;
    pthread_exit(NULL);
}

int send_message(const char *message)
{
    struct sockaddr_in broadcast_addr;
    char hostname[256];
    char full_message[BUFFER_SIZE];

    if (!en_fonctionnement) {
        return -1; // Ne pas envoyer si arrêt en cours
    }

    snprintf(full_message, sizeof(full_message), "[%s] %s", hostname, message);

    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(BROADCAST_PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr(BROADCAST_IP);

    if (sendto(socket_diffusion, full_message, strlen(full_message), 0,
               (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0)
    {
        if (en_fonctionnement)
        { // Ne pas afficher l'erreur si arrêt en cours
            perror("Erreur sendto");
        }
        return -1;
    }
    printf("Message envoyé: %s\n", message);
    return 0;
}

// Function to discover network interfaces
int discover_interfaces()
{
    FILE *fp;
    char line[256];
    char interface_name[32] = {0}, ip[32] = {0}, broadcast[32] = {0};
    nombre_interfaces = 0;

    fp = popen("ip -o -4 addr show | awk '{print $2,$4}'", "r");
    if (fp == NULL) {
        perror("Erreur popen ip a");
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL && nombre_interfaces < MAX_INTERFACES) {
        if (sscanf(line, "%31s %31s", interface_name, ip) == 2) {
            char *slash = strchr(ip, '/');
            if (slash) *slash = '\0';

            char *dot1 = strchr(ip, '.');
            char *dot2 = dot1 ? strchr(dot1 + 1, '.') : NULL;
            char *dot3 = dot2 ? strchr(dot2 + 1, '.') : NULL;
            if (dot1 && dot2 && dot3) {
                int a = atoi(ip);
                int b = atoi(dot1 + 1);
                int c = atoi(dot2 + 1);
                snprintf(broadcast, sizeof(broadcast), "%d.%d.%d.255", a, b, c);
            } else {
                strcpy(broadcast, "255.255.255.255");
            }

            strcpy(interfaces[nombre_interfaces].nom, interface_name);
            strcpy(interfaces[nombre_interfaces].ip_locale, ip);
            strcpy(interfaces[nombre_interfaces].ip_diffusion, broadcast);
            interfaces[nombre_interfaces].active = 1;
            nombre_interfaces++;

            printf("Nouvelle interface: %s (%s) -> broadcast %s\n",
                   interface_name, ip, broadcast);
        }
    }
    pclose(fp);
    return nombre_interfaces;
}

int create_broadcast_socket()
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

void ensure_local_routes()
{
    for (int i = 0; i < nombre_interfaces; i++) {
        // Construire le préfixe réseau (ex : 192.168.1.0/24)
        char prefix[32];
        strcpy(prefix, interfaces[i].ip_locale);
        char *last_dot = strrchr(prefix, '.');
        if (last_dot) strcpy(last_dot + 1, "0/24");

        // Vérifier si la route existe déjà
        char check_cmd[128];
        snprintf(check_cmd, sizeof(check_cmd),
            "ip route show | grep -q '^%s '", prefix);
        int exists = system(check_cmd);

        if (exists != 0) {
            // Ajouter la route
            char add_cmd[256];
            snprintf(add_cmd, sizeof(add_cmd),
                "ip route add %s dev %s", prefix, interfaces[i].nom);
            printf("🛣️  Ajout de la route locale : %s\n", add_cmd);
            system(add_cmd);
        }
    }
}

void join_or_cancel(pthread_t tid, const char *name, struct timespec *timeout) {
    if (pthread_timedjoin_np(tid, NULL, timeout) != 0) {
        printf("Fin du thread %s\n", name);
        pthread_cancel(tid);
    }
}
