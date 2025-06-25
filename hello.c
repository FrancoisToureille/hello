//hello.c
#include "types.h"
#include "hello.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

void *thread_hello(void *arg)
{
    char hostname[256];
    char hello_message[BUFFER_SIZE];

    while (running)
    {
        for (int i = 0; i < interface_count; i++)
        {
            if (interfaces[i].is_active)
            {
                snprintf(hello_message, sizeof(hello_message), "HELLO|%s|%s|%d", hostname, interfaces[i].ip_address, (int)time(NULL));

                printf("  -> Interface %s (%s) vers %s\n",
                       interfaces[i].name, interfaces[i].ip_address, interfaces[i].broadcast_ip);

                int hello_socket = create_broadcast_socket();
                if (hello_socket >= 0)
                {
                    struct sockaddr_in broadcast_addr;
                    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
                    broadcast_addr.sin_family = AF_INET;
                    broadcast_addr.sin_port = htons(BROADCAST_PORT);
                    broadcast_addr.sin_addr.s_addr = inet_addr(interfaces[i].broadcast_ip);

                    if (sendto(hello_socket, hello_message, strlen(hello_message), 0,
                               (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0)
                    {
                        perror("Echec du Hello");
                    }
                    else
                    {
                        printf("  ✅ Hello envoyé sur %s\n", interfaces[i].broadcast_ip);
                    }
                    close(hello_socket);
                }
                else
                {
                    printf("  ❌ Erreur création socket Hello\n");
                }
            }
        }

        cleanup_expired_neighbors();

        initialize_own_lsa();

        sleep(HELLO_INTERVAL);
    }

    pthread_exit(NULL);
}


void process_hello_message(const char *message, const char *sender_ip)
{
    printf("🔍 Message reçu: %s (de %s)\n", message, sender_ip);

    if (strncmp(message, "HELLO|", 6) == 0)
    {
        char *msg_ptr = (char *)message + 6; // Skip "HELLO|"
        char *router_id_end = strchr(msg_ptr, '|');

        if (router_id_end)
        {
            size_t router_id_len = router_id_end - msg_ptr;
            char router_id[32];
            if (router_id_len < sizeof(router_id))
            {
                strncpy(router_id, msg_ptr, router_id_len);
                router_id[router_id_len] = '\0';
                printf("📛 router_id extrait: %s\n", router_id);

                char hostname[256];
                if (gethostname(hostname, sizeof(hostname)) != 0)
                {
                    strcpy(hostname, "Unknown");
                }
                printf("🖥️  Mon hostname local: %s\n", hostname);

                if (strcmp(router_id, hostname) == 0)
                {
                    printf("⚠️  HELLO ignoré : vient de moi-même (%s)\n", router_id);
                    return;
                }

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
                        printf("🌐 IP du voisin extraite: %s\n", router_ip);

                        pthread_mutex_lock(&neighbor_mutex);

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
                            printf("🔄 Mise à jour voisin existant: %s\n", router_id);
                        }
                        else if (neighbor_count < MAX_NEIGHBORS)
                        {
                            printf("➕ Ajout nouveau voisin %s (%s)\n", router_id, router_ip);
                            strcpy(neighbors[neighbor_count].router_id, router_id);
                            strcpy(neighbors[neighbor_count].ip_address, router_ip);
                            neighbors[neighbor_count].metric = 1;
                            neighbors[neighbor_count].last_hello = time(NULL);
                            neighbors[neighbor_count].bandwidth_mbps = 100;
                            neighbors[neighbor_count].link_state = 1;

                            int matched = 0;
                            for (int j = 0; j < interface_count; j++)
                            {
                                printf("🔍 Test interface %s (%s) avec IP %s\n", interfaces[j].name, interfaces[j].ip_address, router_ip);

                                if (strncmp(router_ip, interfaces[j].ip_address, strlen(interfaces[j].ip_address) - 2) == 0)
                                {
                                    strcpy(neighbors[neighbor_count].interface, interfaces[j].name);
                                    matched = 1;
                                    break;
                                }
                            }

                            if (!matched)
                            {
                                // Patch de secours
                                strcpy(neighbors[neighbor_count].interface, interfaces[0].name);
                                printf("⚠️  Aucune interface correspondante. Utilisation de %s par défaut.\n", interfaces[0].name);
                            }

                            printf("🤝 Nouveau voisin découvert: %s (%s via %s)\n",
                                   router_id, router_ip, neighbors[neighbor_count].interface);

                            neighbor_count++;
                        }
                        else
                        {
                            printf("🚫 Impossible d'ajouter le voisin : MAX_NEIGHBORS atteint.\n");
                        }

                        pthread_mutex_unlock(&neighbor_mutex);
                    }
                }
            }
        }
    }
}