#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <time.h>

#include "types.h"
#include "lsa.h"
#include "hello.h"
#include "routing.h"
#include "control.h"
#include "dijkstra.h"

void *thread_lsa(void *arg)
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }
    char lsa_message[LEN_BUFFER];

    while (running)
    {
        pthread_mutex_lock(&neighbor_mutex);

        snprintf(lsa_message, sizeof(lsa_message), "LSA|%s|%d|%d",
                 hostname, (int)time(NULL), neighbor_count + interface_count);

        for (int i = 0; i < neighbor_count; i++)
        {
            if (neighbors[i].etat_lien == 1)
            {
                char link_info[128];
                snprintf(link_info, sizeof(link_info), "|%s,%s,%d,%d",
                         neighbors[i].id_routeur, neighbors[i].adresse_ip,
                         neighbors[i].metrique, neighbors[i].bandwidth_mbps);
                strcat(lsa_message, link_info);
            }
        }
        
        for (int i = 0; i < interface_count; i++)
        {
            if (interfaces[i].active)
            {
                char interface_info[128];
                snprintf(interface_info, sizeof(interface_info), "|%s,%s,%d,%d",
                         hostname, interfaces[i].adresse_ip, 1, 1000);
                strcat(lsa_message, interface_info);
            }
        }

        pthread_mutex_unlock(&neighbor_mutex);

        lsa_diffusion(lsa_message);
        printf("LSA diffusé: %d voisins + %d interfaces\n", neighbor_count, interface_count);

        sleep(30); 
    }

    pthread_exit(NULL);
}

void flow_lsa(const char *lsa_message, const char *sender_ip)
{
    int flood_sock = creer_socket_diffusion();
    if (flood_sock < 0)
        return;

    pthread_mutex_lock(&neighbor_mutex);
    for (int i = 0; i < neighbor_count; i++)
    {
        if (neighbors[i].etat_lien == 1 &&
            strcmp(neighbors[i].adresse_ip, sender_ip) != 0)
        {

            struct sockaddr_in neighbor_addr;
            memset(&neighbor_addr, 0, sizeof(neighbor_addr));
            neighbor_addr.sin_family = AF_INET;
            neighbor_addr.sin_port = htons(BROADCAST_PORT);
            neighbor_addr.sin_addr.s_addr = inet_addr(neighbors[i].adresse_ip);

            sendto(flood_sock, lsa_message, strlen(lsa_message), 0,
                   (struct sockaddr *)&neighbor_addr, sizeof(neighbor_addr));
        }
    }
    pthread_mutex_unlock(&neighbor_mutex);
    close(flood_sock);
}

void processus_lsa(const char *message, const char *sender_ip)
{
    char msg_copy[LEN_BUFFER];
    strncpy(msg_copy, message, sizeof(msg_copy));
    msg_copy[sizeof(msg_copy) - 1] = '\0';

    char *saveptr;
    char *token = strtok_r(msg_copy, "|", &saveptr); 
    if (!token || strcmp(token, "LSA") != 0)
        return;

    char *lsa_router_id = strtok_r(NULL, "|", &saveptr);
    char *lsa_timestamp_str = strtok_r(NULL, "|", &saveptr);
    char *lsa_num_links_str = strtok_r(NULL, "|", &saveptr);

    if (!lsa_router_id || !lsa_timestamp_str || !lsa_num_links_str)
        return;

    int lsa_num_links = atoi(lsa_num_links_str);

    lsa_t new_lsa;
    memset(&new_lsa, 0, sizeof(new_lsa));
    strncpy(new_lsa.id_routeur, lsa_router_id, sizeof(new_lsa.id_routeur) - 1);
    new_lsa.timestamp = atoi(lsa_timestamp_str);
    new_lsa.num_links = 0;

    for (int i = 0; i < lsa_num_links; i++)
    {
        char *link_str = strtok_r(NULL, "|", &saveptr);
        if (!link_str)
            break;
        
        char *field_ptr;
        char *id_routeur = strtok_r(link_str, ",", &field_ptr);
        char *ip = strtok_r(NULL, ",", &field_ptr);
        char *metric_str = strtok_r(NULL, ",", &field_ptr);
        char *bw_str = strtok_r(NULL, ",", &field_ptr);
        
        if (!id_routeur || !ip || !metric_str || !bw_str)
            continue;
            
        voisin_t link;
        memset(&link, 0, sizeof(link));
        strncpy(link.id_routeur, id_routeur, sizeof(link.id_routeur) - 1);
        strncpy(link.adresse_ip, ip, sizeof(link.adresse_ip) - 1);
        link.metrique = atoi(metric_str);
        link.bandwidth_mbps = atoi(bw_str);
        link.etat_lien = 1;
        
        for (int j = 0; j < interface_count; j++) {
            char interface_network[32], link_network[32];
            strcpy(interface_network, interfaces[j].adresse_ip);
            strcpy(link_network, ip);
            
            char *dot = strrchr(interface_network, '.');
            if (dot) strcpy(dot + 1, "0");
            dot = strrchr(link_network, '.');
            if (dot) strcpy(dot + 1, "0");
            
            if (strcmp(interface_network, link_network) == 0) {
                strcpy(link.interface, interfaces[j].nom);
                break;
            }
        }
        
        new_lsa.links[new_lsa.num_links++] = link;
    }
    
    int updated = 0;
    pthread_mutex_lock(&topology_mutex);
    
    int found = -1;
    for (int i = 0; i < topology_db_size; i++)
    {
        if (strcmp(topology_db[i].id_routeur, new_lsa.id_routeur) == 0)
        {
            if (new_lsa.timestamp > topology_db[i].timestamp)
            {
                topology_db[i] = new_lsa;
                updated = 1;
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

    if (updated)
    {
        printf("LSA mis à jour pour %s (%d liens)\n", lsa_router_id, lsa_num_links);
        calcul_chemins();
        
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) != 0)
            strcpy(hostname, "Unknown");
        
        if (strcmp(lsa_router_id, hostname) != 0) {
            flow_lsa(message, sender_ip);
        }
    }
}

void init_lsa()
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }
    pthread_mutex_lock(&topology_mutex);

    int our_lsa_index = -1;
    for (int i = 0; i < topology_db_size; i++)
    {
        if (strcmp(topology_db[i].id_routeur, hostname) == 0)
        {
            our_lsa_index = i;
            break;
        }
    }

    if (our_lsa_index < 0 && topology_db_size < MAX_NEIGHBORS)
    {
        our_lsa_index = topology_db_size;
        topology_db_size++;
    }

    if (our_lsa_index >= 0)
    {
        strcpy(topology_db[our_lsa_index].id_routeur, hostname);
        topology_db[our_lsa_index].sequence_number = (int)time(NULL);
        topology_db[our_lsa_index].timestamp = time(NULL);
        topology_db[our_lsa_index].num_links = 0;

        pthread_mutex_lock(&neighbor_mutex);
        for (int i = 0; i < neighbor_count && topology_db[our_lsa_index].num_links < MAX_NEIGHBORS; i++)
        {
            if (neighbors[i].etat_lien == 1)
            {
                int link_idx = topology_db[our_lsa_index].num_links;
                topology_db[our_lsa_index].links[link_idx] = neighbors[i];
                topology_db[our_lsa_index].num_links++;
            }
        }
        pthread_mutex_unlock(&neighbor_mutex);
        
        for (int i = 0; i < interface_count && topology_db[our_lsa_index].num_links < MAX_NEIGHBORS; i++)
        {
            if (interfaces[i].active)
            {
                int link_idx = topology_db[our_lsa_index].num_links;
                voisin_t interface_link;
                memset(&interface_link, 0, sizeof(interface_link));
                
                strcpy(interface_link.id_routeur, hostname);
                strcpy(interface_link.adresse_ip, interfaces[i].adresse_ip);
                strcpy(interface_link.interface, interfaces[i].nom);
                interface_link.metrique = 1;
                interface_link.bandwidth_mbps = 1000;
                interface_link.etat_lien = 1;
                interface_link.dernier_hello = time(NULL);
                
                topology_db[our_lsa_index].links[link_idx] = interface_link;
                topology_db[our_lsa_index].num_links++;
            }
        }
    }

    pthread_mutex_unlock(&topology_mutex);
}

void lsa_diffusion(const char *lsa_message)
{
    struct sockaddr_in broadcast_addr;

    for (int i = 0; i < interface_count; i++)
    {
        if (interfaces[i].active)
        {
            memset(&broadcast_addr, 0, sizeof(broadcast_addr));
            broadcast_addr.sin_family = AF_INET;
            broadcast_addr.sin_port = htons(BROADCAST_PORT);
            broadcast_addr.sin_addr.s_addr = inet_addr(interfaces[i].ip_diffusion);

            sendto(broadcast_sock, lsa_message, strlen(lsa_message), 0,
                   (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        }
    }
}
