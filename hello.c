#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdlib.h>
#include "types.h"
#include "hello.h"
#include "lsa.h"
#include "control.h"

void ensure_local_routes()
{
    for (int i = 0; i < interface_count; i++) {
        // Construire le préfixe réseau (ex : 192.168.1.0/24)
        char prefix[32];
        strcpy(prefix, interfaces[i].ip_address);
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
                "ip route add %s dev %s", prefix, interfaces[i].name);
            printf("🛣️  Ajout de la route locale : %s\n", add_cmd);
            system(add_cmd);
        }
    }
}
// Function to discover network interfaces
int discover_interfaces()
{
    FILE *fp;
    char line[256];
    char interface_name[16] = {0}, ip[16] = {0}, broadcast[16] = {0};
    interface_count = 0;

    fp = popen("ip -o -4 addr show | awk '{print $2,$4}'", "r");
    if (fp == NULL) {
        perror("Erreur popen ip a");
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL && interface_count < MAX_INTERFACES) {
        // Format attendu : eth0 192.168.1.1/24
        if (sscanf(line, "%15s %15s", interface_name, ip) == 2) {
            // Retirer le /xx du préfixe
            char *slash = strchr(ip, '/');
            if (slash) *slash = '\0';

            // Calculer l'adresse de broadcast (optionnel, à adapter si besoin)
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

            strcpy(interfaces[interface_count].name, interface_name);
            strcpy(interfaces[interface_count].ip_address, ip);
            strcpy(interfaces[interface_count].broadcast_ip, broadcast);
            interfaces[interface_count].is_active = 1;
            interface_count++;

            printf("🔍 Interface découverte: %s (%s) -> broadcast %s\n",
                   interface_name, ip, broadcast);
        }
    }
    pclose(fp);

    return interface_count;
}

// Function to process hello messages
void process_hello_message(const char *message, const char *sender_ip)
{
    printf("🔍 Message reçu: %s (de %s)\n", message, sender_ip);

    if (strncmp(message, "HELLO|", 6) == 0)
    {
        char *msg_ptr = (char *)message + 6; // Skip "HELLO|"
        char *router_id_end = strchr(msg_ptr, '|');

        if (router_id_end)
        {
            // Extract router_id
            size_t router_id_len = router_id_end - msg_ptr;
            char router_id[32];
            if (router_id_len < sizeof(router_id))
            {
                strncpy(router_id, msg_ptr, router_id_len);
                router_id[router_id_len] = '\0';

                // Vérifier si le HELLO vient de nous-même
                char hostname[256];
                if (gethostname(hostname, sizeof(hostname)) != 0)
                {
                    strcpy(hostname, "Unknown");
                }
                if (strcmp(router_id, hostname) == 0)
                {
                    // On ignore notre propre HELLO
                    return;
                }

                // Extract IP address
                char *ip_start = router_id_end + 1;
                char *ip_end = strchr(ip_start, '|');

                if (ip_end)
                {
                    size_t ip_len = ip_end - ip_start;
                    char router_ip[16];
                    if (ip_len < sizeof(router_ip))
                    {
                        strncpy(router_ip, ip_start, ip_len);
                        router_ip[ip_len] = '\0';

                        pthread_mutex_lock(&neighbor_mutex);

                        // Check if neighbor already exists
                        int found = -1;
                        for (int i = 0; i < neighbor_count; i++)
                        {
                            if (strcmp(neighbors[i].router_id, router_id) == 0)
                            {
                                found = i;
                                break;
                            }
                        }

                        if (found >= 0)
                        {
                            neighbors[found].last_hello = time(NULL);
                            neighbors[found].link_state = 1;
                            printf("🔄 Mise à jour voisin: %s\n", router_id);
                        }
                        else if (neighbor_count < MAX_NEIGHBORS)
                        {
                            strcpy(neighbors[neighbor_count].router_id, router_id);
                            strcpy(neighbors[neighbor_count].ip_address, router_ip);
                            neighbors[neighbor_count].metric = 1;
                            neighbors[neighbor_count].last_hello = time(NULL);
                            neighbors[neighbor_count].bandwidth_mbps = 100;
                            neighbors[neighbor_count].link_state = 1;

                            for (int j = 0; j < interface_count; j++)
                            {
                                if (strncmp(router_ip, interfaces[j].ip_address, strlen(interfaces[j].ip_address) - 2) == 0)
                                {
                                    strcpy(neighbors[neighbor_count].interface, interfaces[j].name);
                                    break;
                                }
                            }

                            printf("🤝 Nouveau voisin découvert: %s (%s)\n", router_id, router_ip);
                            neighbor_count++;
                        }

                        pthread_mutex_unlock(&neighbor_mutex);
                    }
                }
            }
        }
    }
}


// Fonction pour nettoyer les voisins expirés
void cleanup_expired_neighbors()
{
    pthread_mutex_lock(&neighbor_mutex);

    time_t now = time(NULL);
    for (int i = 0; i < neighbor_count; i++)
    {
        if (now - neighbors[i].last_hello > NEIGHBOR_TIMEOUT)
        {
            printf("❌ Voisin expiré: %s\n", neighbors[i].router_id);
            neighbors[i].link_state = 0;
        }
    }

    pthread_mutex_unlock(&neighbor_mutex);
}

// Thread pour envoyer des messages Hello périodiques
void *hello_thread(void *arg)
{
    char hostname[256];
    char hello_message[BUFFER_SIZE];

    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    while (running)
    {
        printf("📡 Envoi des messages Hello...\n");

        for (int i = 0; i < interface_count; i++)
        {
            if (interfaces[i].is_active)
            {
                snprintf(hello_message, sizeof(hello_message),
                         "HELLO|%s|%s|%d", hostname, interfaces[i].ip_address, (int)time(NULL));

                printf("  -> Interface %s (%s) vers %s\n",
                       interfaces[i].name, interfaces[i].ip_address, interfaces[i].broadcast_ip);

                int hello_sock = create_broadcast_socket();
                if (hello_sock >= 0)
                {
                    struct sockaddr_in broadcast_addr;
                    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
                    broadcast_addr.sin_family = AF_INET;
                    broadcast_addr.sin_port = htons(BROADCAST_PORT);
                    broadcast_addr.sin_addr.s_addr = inet_addr(interfaces[i].broadcast_ip);

                    if (sendto(hello_sock, hello_message, strlen(hello_message), 0,
                               (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0)
                    {
                        perror("Erreur envoi Hello");
                    }
                    else
                    {
                        printf("  ✅ Hello envoyé sur %s\n", interfaces[i].broadcast_ip);
                    }

                    close(hello_sock);
                }
                else
                {
                    printf("  ❌ Erreur création socket Hello\n");
                }
            }
        }

        cleanup_expired_neighbors();

        // Mettre à jour notre LSA avec les nouveaux voisins
        initialize_own_lsa();

        sleep(HELLO_INTERVAL);
    }

    pthread_exit(NULL);
}