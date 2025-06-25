//lsa.c
#include "types.h"
#include "lsa.h"
#include "routing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

// Thread pour échanger les LSA
void *lsa_thread(void *arg)
{
    char hostname[256];
    char lsa_message[BUFFER_SIZE];

    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    while (running)
    {
        // Créer et envoyer notre LSA
        pthread_mutex_lock(&neighbor_mutex);

        snprintf(lsa_message, sizeof(lsa_message), "LSA|%s|%d|%d",
                 hostname, (int)time(NULL), neighbor_count);

        // Ajouter les informations de chaque voisin
        for (int i = 0; i < neighbor_count; i++)
        {
            if (neighbors[i].link_state == 1)
            {
                char link_info[128];
                snprintf(link_info, sizeof(link_info), "|%s,%s,%d,%d",
                         neighbors[i].router_id, neighbors[i].ip_address,
                         neighbors[i].metric, neighbors[i].bandwidth_mbps);
                strcat(lsa_message, link_info);
            }
        }

        pthread_mutex_unlock(&neighbor_mutex);

        // Envoyer LSA sur toutes les interfaces
        broadcast_lsa(lsa_message);

        sleep(30); // Envoyer LSA toutes les 30 secondes
    }

    pthread_exit(NULL);
}


void flood_lsa(const char *lsa_message, const char *sender_ip)
{
    int flood_sock = create_broadcast_socket();
    if (flood_sock < 0)
        return;

    pthread_mutex_lock(&neighbor_mutex);
    for (int i = 0; i < neighbor_count; i++)
    {
        if (neighbors[i].link_state == 1 &&
            strcmp(neighbors[i].ip_address, sender_ip) != 0)
        {

            struct sockaddr_in neighbor_addr;
            memset(&neighbor_addr, 0, sizeof(neighbor_addr));
            neighbor_addr.sin_family = AF_INET;
            neighbor_addr.sin_port = htons(BROADCAST_PORT);
            neighbor_addr.sin_addr.s_addr = inet_addr(neighbors[i].ip_address);

            sendto(flood_sock, lsa_message, strlen(lsa_message), 0,
                   (struct sockaddr *)&neighbor_addr, sizeof(neighbor_addr));
        }
    }
    pthread_mutex_unlock(&neighbor_mutex);
    close(flood_sock);
}

// Function to process LSA messages
void process_lsa_message(const char *message, const char *sender_ip)
{
    // Format: LSA|router_id|timestamp|num_links|router_id,ip,metric,bandwidth|...
    char msg_copy[BUFFER_SIZE];
    strncpy(msg_copy, message, sizeof(msg_copy));
    msg_copy[sizeof(msg_copy) - 1] = '\0';

    char *saveptr;
    char *token = strtok_r(msg_copy, "|", &saveptr); // "LSA"
    if (!token || strcmp(token, "LSA") != 0)
        return;

    char *lsa_router_id = strtok_r(NULL, "|", &saveptr);
    char *lsa_timestamp_str = strtok_r(NULL, "|", &saveptr);
    char *lsa_num_links_str = strtok_r(NULL, "|", &saveptr);

    if (!lsa_router_id || !lsa_timestamp_str || !lsa_num_links_str)
        return;

    // Ignorer nos propres LSA
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
        strcpy(hostname, "Unknown");

    if (strcmp(lsa_router_id, hostname) == 0)
    {
        printf("🔇 Ignoré LSA de moi-même (%s)\n", hostname);
        return;
    }

    int lsa_num_links = atoi(lsa_num_links_str);

    lsa_t new_lsa;
    memset(&new_lsa, 0, sizeof(new_lsa));
    strncpy(new_lsa.router_id, lsa_router_id, sizeof(new_lsa.router_id) - 1);
    new_lsa.timestamp = atoi(lsa_timestamp_str);
    new_lsa.num_links = 0;

    for (int i = 0; i < lsa_num_links; i++)
    {
        char *link_str = strtok_r(NULL, "|", &saveptr);
        if (!link_str)
            break;
        char *field_ptr;
        char *router_id = strtok_r(link_str, ",", &field_ptr);
        char *ip = strtok_r(NULL, ",", &field_ptr);
        char *metric_str = strtok_r(NULL, ",", &field_ptr);
        char *bw_str = strtok_r(NULL, ",", &field_ptr);
        if (!router_id || !ip || !metric_str || !bw_str)
            continue;
        neighbor_t link;
        memset(&link, 0, sizeof(link));
        strncpy(link.router_id, router_id, sizeof(link.router_id) - 1);
        strncpy(link.ip_address, ip, sizeof(link.ip_address) - 1);
        link.metric = atoi(metric_str);
        link.bandwidth_mbps = atoi(bw_str);
        link.link_state = 1;
        new_lsa.links[new_lsa.num_links++] = link;
    }
    int updated = 0;
    pthread_mutex_lock(&topology_mutex);
    int found = -1;
    for (int i = 0; i < topology_db_size; i++)
    {
        if (strcmp(topology_db[i].router_id, new_lsa.router_id) == 0)
        {
            // Remplacer si timestamp plus récent
            if (new_lsa.timestamp > topology_db[i].timestamp)
            {
                topology_db[i] = new_lsa;
                updated = 1; // LSDB modifiée
            }
            found = 1;
            break;
        }
    }
    if (found == -1 && topology_db_size < MAX_NEIGHBORS)
    {
        topology_db[topology_db_size++] = new_lsa;
        updated = 1;
    }
    pthread_mutex_unlock(&topology_mutex);

    // NE recalculer/flooder QUE si la LSDB a changé
    if (updated)
    {
        calculate_shortest_paths();
        flood_lsa(message, sender_ip);
    }
}

// Fonction pour créer notre propre LSA dans la base de données
void initialize_own_lsa()
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    pthread_mutex_lock(&topology_mutex);

    // Vérifier si notre LSA existe déjà
    int our_lsa_index = -1;
    for (int i = 0; i < topology_db_size; i++)
    {
        if (strcmp(topology_db[i].router_id, hostname) == 0)
        {
            our_lsa_index = i;
            break;
        }
    }

    // Créer notre LSA si elle n'existe pas
    if (our_lsa_index < 0 && topology_db_size < MAX_NEIGHBORS)
    {
        our_lsa_index = topology_db_size;
        topology_db_size++;
    }

    if (our_lsa_index >= 0)
    {
        strcpy(topology_db[our_lsa_index].router_id, hostname);
        topology_db[our_lsa_index].sequence_number = (int)time(NULL);
        topology_db[our_lsa_index].timestamp = time(NULL);
        topology_db[our_lsa_index].num_links = 0;

        // Ajouter nos voisins directs
        pthread_mutex_lock(&neighbor_mutex);
        for (int i = 0; i < neighbor_count && topology_db[our_lsa_index].num_links < MAX_NEIGHBORS; i++)
        {
            if (neighbors[i].link_state == 1)
            {
                int link_idx = topology_db[our_lsa_index].num_links;
                topology_db[our_lsa_index].links[link_idx] = neighbors[i];
                topology_db[our_lsa_index].num_links++;
            }
        }
        pthread_mutex_unlock(&neighbor_mutex);
    }

    pthread_mutex_unlock(&topology_mutex);
}


// Function to broadcast LSA messages
void broadcast_lsa(const char *lsa_message)
{
    struct sockaddr_in broadcast_addr;

    for (int i = 0; i < interface_count; i++)
    {
        if (interfaces[i].is_active)
        {
            memset(&broadcast_addr, 0, sizeof(broadcast_addr));
            broadcast_addr.sin_family = AF_INET;
            broadcast_addr.sin_port = htons(BROADCAST_PORT);
            broadcast_addr.sin_addr.s_addr = inet_addr(interfaces[i].broadcast_ip);

            sendto(broadcast_sock, lsa_message, strlen(lsa_message), 0,
                   (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        }
    }
}