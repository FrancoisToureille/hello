include "types.h"
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

// Variables globales externes supposées déclarées ailleurs
extern int running;
extern int broadcast_sock;
extern int listen_sock;
extern pthread_mutex_t neighbor_mutex;
extern pthread_mutex_t topology_mutex;
extern pthread_mutex_t routing_mutex;

extern neighbor_t neighbors[MAX_NEIGHBORS];
extern lsa_t topology_db[MAX_TOPOLOGY];
extern int neighbor_count;
extern int topology_db_size;
extern int interface_count;
extern interface_t interfaces[MAX_INTERFACES];

// Gestion du signal d'arrêt
void signal_handler(int sig)
{
    running = 0;
    printf("\nArrêt en cours..\n");

    if (broadcast_sock >= 0)
        close(broadcast_sock);

    if (listen_sock >= 0)
        close(listen_sock);
}

// Verrouillage/déverrouillage ordonné des mutex
void lock_all_mutexes(void)
{
    pthread_mutex_lock(&neighbor_mutex);
    pthread_mutex_lock(&topology_mutex);
    pthread_mutex_lock(&routing_mutex);
}

void unlock_all_mutexes(void)
{
    pthread_mutex_unlock(&routing_mutex);
    pthread_mutex_unlock(&topology_mutex);
    pthread_mutex_unlock(&neighbor_mutex);
}

// Thread d'écoute des messages UDP broadcast
void *listen_thread(void *arg)
{
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    socklen_t client_len = sizeof(client_addr);
    ssize_t bytes_received;
    fd_set readfds;
    struct timeval timeout;

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
        strcpy(hostname, "Unknown");

    listen_sock = create_broadcast_socket();
    if (listen_sock < 0)
        pthread_exit(NULL);

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

    printf("🔊 Écoute active sur le port %d\n", BROADCAST_PORT);

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
                break;

            perror("Erreur select");
            break;
        }
        else if (select_result == 0)
            continue;

        if (FD_ISSET(listen_sock, &readfds))
        {
            bytes_received = recvfrom(listen_sock, buffer, sizeof(buffer) - 1, 0,
                                      (struct sockaddr *)&client_addr, &client_len);
            if (bytes_received > 0)
            {
                buffer[bytes_received] = '\0';

                if (strncmp(buffer, "HELLO|", 6) == 0)
                    process_hello_message(buffer, inet_ntoa(client_addr.sin_addr));
                else if (strncmp(buffer, "LSA|", 4) == 0)
                    process_lsa_message(buffer, inet_ntoa(client_addr.sin_addr));
                else
                {
                    // Affichage si le message ne vient pas de ce noeud
                    if (strstr(buffer, hostname) != buffer + 1)
                    {
                        time_t now = time(NULL);
                        char *time_str = ctime(&now);
                        time_str[strlen(time_str) - 1] = '\0'; // Supprimer '\n'

                        printf("\n📨 [%s] Reçu de %s: %s\n",
                               time_str, inet_ntoa(client_addr.sin_addr), buffer);
                        printf("💬 Commande: ");
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

// Envoi d’un message broadcast formaté
int send_message(const char *message)
{
    if (!running)
        return -1;

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
        strcpy(hostname, "Unknown");

    char full_message[BUFFER_SIZE];
    snprintf(full_message, sizeof(full_message), "[%s] %s", hostname, message);

    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(BROADCAST_PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr(BROADCAST_IP);

    if (sendto(broadcast_sock, full_message, strlen(full_message), 0,
               (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0)
    {
        if (running)
            perror("Erreur sendto");

        return -1;
    }

    printf("Message envoyé: %s\n", message);
    return 0;
}

// Découverte des interfaces réseau actives (IPv4 uniquement)
int discover_interfaces(void)
{
    FILE *fp = popen("ip -o -4 addr show | awk '{print $2,$4}'", "r");
    if (!fp)
    {
        perror("Erreur popen ip a");
        return -1;
    }

    char line[256];
    interface_count = 0;

    while (fgets(line, sizeof(line), fp) != NULL && interface_count < MAX_INTERFACES)
    {
        char interface_name[16], ip[16];
        if (sscanf(line, "%15s %15s", interface_name, ip) == 2)
        {
            // Suppression du suffixe /xx dans l’adresse IP
            char *slash = strchr(ip, '/');
            if (slash) *slash = '\0';

            // Calcul simplifié de l’adresse broadcast en /24
            char broadcast[16];
            int a, b, c;
            if (sscanf(ip, "%d.%d.%d.%*d", &a, &b, &c) == 3)
                snprintf(broadcast, sizeof(broadcast), "%d.%d.%d.255", a, b, c);
            else
                strcpy(broadcast, "255.255.255.255");

            strcpy(interfaces[interface_count].name, interface_name);
            strcpy(interfaces[interface_count].ip_address, ip);
            strcpy(interfaces[interface_count].broadcast_ip, broadcast);
            interfaces[interface_count].is_active = 1;

            printf("Nouvelle interface: %s (%s) -> broadcast %s\n",
                   interface_name, ip, broadcast);

            interface_count++;
        }
    }
    pclose(fp);

    return interface_count;
}

// Création d’une socket UDP broadcast
int create_broadcast_socket(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("Erreur création socket");
        return -1;
    }

    int broadcast_enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
                   &broadcast_enable, sizeof(broadcast_enable)) < 0)
    {
        perror("Erreur setsockopt SO_BROADCAST");
        close(sock);
        return -1;
    }

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) < 0)
    {
        perror("Erreur setsockopt SO_REUSEADDR");
        close(sock);
        return -1;
    }

    return sock;
}

// Ajoute les routes locales manquantes pour chaque interface active
void ensure_local_routes(void)
{
    for (int i = 0; i < interface_count; i++)
    {
        char prefix[32];
        strcpy(prefix, interfaces[i].ip_address);
        char *last_dot = strrchr(prefix, '.');
        if (last_dot)
            strcpy(last_dot + 1, "0/24");

        char check_cmd[128];
        snprintf(check_cmd, sizeof(check_cmd),
                 "ip route show | grep -q '^%s '", prefix);
        int exists = system(check_cmd);

        if (exists != 0)
        {
            char add_cmd[256];
            snprintf(add_cmd, sizeof(add_cmd),
                     "ip route add %s dev %s", prefix, interfaces[i].name);
            printf("🛣️  Ajout de la route locale : %s\n", add_cmd);
            system(add_cmd);
        }
    }
}

// Joint un thread avec timeout, annule si dépassement
void join_or_cancel(pthread_t tid, const char *name, struct timespec *timeout)
{
    if (pthread_timedjoin_np(tid, NULL, timeout) != 0)
    {
        printf("Fin du thread %s\n", name);
        pthread_cancel(tid);
    }
}