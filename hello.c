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

void ajouter_routes_locales()
{
    for (int i = 0; i < interface_count; i++) {
        char prefix[32];
        strcpy(prefix, interfaces[i].adresse_ip);
        char *last_dot = strrchr(prefix, '.');
        if (last_dot) strcpy(last_dot + 1, "0/24");

        char check_cmd[128];
        snprintf(check_cmd, sizeof(check_cmd),
            "ip route show | grep -q '^%s '", prefix);
        int exists = system(check_cmd);

        if (exists != 0) {
            char add_cmd[256];
            snprintf(add_cmd, sizeof(add_cmd),
                "ip route add %s dev %s", prefix, interfaces[i].nom);
            printf("Ajout de la route locale : %s\n", add_cmd);
            system(add_cmd);
        }
    }
}
int voir_interfaces_locales()
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
        if (sscanf(line, "%15s %15s", interface_name, ip) == 2) {
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

            strcpy(interfaces[interface_count].nom, interface_name);
            strcpy(interfaces[interface_count].adresse_ip, ip);
            strcpy(interfaces[interface_count].ip_diffusion, broadcast);
            interfaces[interface_count].active = 1;
            interface_count++;

            printf("Interface découverte: %s (%s) -> broadcast %s\n",
                   interface_name, ip, broadcast);
        }
    }
    pclose(fp);

    return interface_count;
}

void processus_message_hello(const char *message, const char *sender_ip)
{
    printf("Message reçu: %s (de %s)\n", message, sender_ip);

    if (strncmp(message, "HELLO|", 6) == 0)
    {
        char *msg_ptr = (char *)message + 6;
        char *router_id_end = strchr(msg_ptr, '|');

        if (router_id_end)
        {
            size_t router_id_len = router_id_end - msg_ptr;
            char id_routeur[32];
            if (router_id_len < sizeof(id_routeur))
            {
                strncpy(id_routeur, msg_ptr, router_id_len);
                id_routeur[router_id_len] = '\0';

                char hostname[256];
                if (gethostname(hostname, sizeof(hostname)) != 0)
                {
                    strcpy(hostname, "Unknown");
                }
                if (strcmp(id_routeur, hostname) == 0)
                {
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

                        pthread_mutex_lock(&neighbor_mutex);

                        int found = -1;
                        for (int i = 0; i < neighbor_count; i++)
                        {
                            if (strcmp(neighbors[i].id_routeur, id_routeur) == 0)
                            {
                                found = i;
                                break;
                            }
                        }

                        if (found >= 0)
                        {
                            neighbors[found].dernier_hello = time(NULL);
                            neighbors[found].etat_lien = 1;
                            printf("🔄 Mise à jour voisin: %s\n", id_routeur);
                        }
                        else if (neighbor_count < MAX_NEIGHBORS)
                        {
                            strcpy(neighbors[neighbor_count].id_routeur, id_routeur);
                            strcpy(neighbors[neighbor_count].adresse_ip, router_ip);
                            neighbors[neighbor_count].metrique = 1;
                            neighbors[neighbor_count].dernier_hello = time(NULL);
                            neighbors[neighbor_count].bandwidth_mbps = 100;
                            neighbors[neighbor_count].etat_lien = 1;

                            for (int j = 0; j < interface_count; j++)
                            {
                                if (strncmp(router_ip, interfaces[j].adresse_ip, strlen(interfaces[j].adresse_ip) - 2) == 0)
                                {
                                    strcpy(neighbors[neighbor_count].interface, interfaces[j].nom);
                                    break;
                                }
                            }

                            printf("Nouveau voisin : %s (%s)\n", id_routeur, router_ip);
                            neighbor_count++;
                        }

                        pthread_mutex_unlock(&neighbor_mutex);
                    }
                }
            }
        }
    }
}

void supprimer_voisins_down()
{
    pthread_mutex_lock(&neighbor_mutex);

    time_t now = time(NULL);
    for (int i = 0; i < neighbor_count; i++)
    {
        if (now - neighbors[i].dernier_hello > LIMITE_VOISIN)
        {
            printf("Voisin expiré: %s\n", neighbors[i].id_routeur);
            neighbors[i].etat_lien = 0;
        }
    }

    pthread_mutex_unlock(&neighbor_mutex);
}

void *thread_hello(void *arg)
{
    char hostname[256];
    char hello_message[LEN_BUFFER];

    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    while (running)
    {
        printf("Envoi des messages Hello...\n");

        for (int i = 0; i < interface_count; i++)
        {
            if (interfaces[i].active)
            {
                snprintf(hello_message, sizeof(hello_message),
                         "HELLO|%s|%s|%d", hostname, interfaces[i].adresse_ip, (int)time(NULL));

                printf("  -> Interface %s (%s) vers %s\n",
                       interfaces[i].nom, interfaces[i].adresse_ip, interfaces[i].ip_diffusion);

                int hello_sock = creer_socket_diffusion();
                if (hello_sock >= 0)
                {
                    struct sockaddr_in broadcast_addr;
                    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
                    broadcast_addr.sin_family = AF_INET;
                    broadcast_addr.sin_port = htons(BROADCAST_PORT);
                    broadcast_addr.sin_addr.s_addr = inet_addr(interfaces[i].ip_diffusion);

                    if (sendto(hello_sock, hello_message, strlen(hello_message), 0,
                               (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0)
                    {
                        perror("Erreur envoi Hello");
                    }
                    else
                    {
                        printf("On envoie Hello sur %s\n", interfaces[i].ip_diffusion);
                    }

                    close(hello_sock);
                }
                else
                {
                    printf("Erreur lors de la création de la socket Hello\n");
                }
            }
        }
        supprimer_voisins_down();
        init_lsa();
        sleep(HELLO_INTERVAL);
    }

    pthread_exit(NULL);
}