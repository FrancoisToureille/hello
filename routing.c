#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>

#include "types.h"
#include "routing.h"

void lock_all_mutexes()
{
    pthread_mutex_lock(&neighbor_mutex);
    pthread_mutex_lock(&topology_mutex);
    pthread_mutex_lock(&routing_mutex);
}

void unlock_all_mutexes()
{
    pthread_mutex_unlock(&routing_mutex);
    pthread_mutex_unlock(&topology_mutex);
    pthread_mutex_unlock(&neighbor_mutex);
}

void construite_table_routage(noeud_dijkstra_t *nodes, int node_count, int source_index) {
    route_count = 0;
    for (int i = 0; i < node_count; i++) {
        if (i == source_index) continue;

        for (int j = 0; j < topology_db_size; j++) {
            if (strcmp(topology_db[j].id_routeur, nodes[i].id_routeur) != 0) continue;

            for (int k = 0; k < topology_db[j].num_links; k++) {
                const char *dest_ip = topology_db[j].links[k].adresse_ip;

                int is_own_ip = 0;
                for (int m = 0; m < interface_count; m++) {
                    if (strcmp(dest_ip, interfaces[m].adresse_ip) == 0) {
                        is_own_ip = 1;
                        break;
                    }
                }
                if (is_own_ip)
                    continue;

                char prefix[32];
                strcpy(prefix, dest_ip);
                char *last_dot = strrchr(prefix, '.');
                if (last_dot) strcpy(last_dot + 1, "0/24");

                int already = 0;
                for (int r = 0; r < route_count; r++) {
                    if (strcmp(routing_table[r].destination, prefix) == 0) {
                        already = 1;
                        break;
                    }
                }
                if (already)
                    continue;

                int is_own_network = 0;
                for (int m = 0; m < interface_count; m++) {
                    char local_prefix[32];
                    strcpy(local_prefix, interfaces[m].adresse_ip);
                    char *last_dot = strrchr(local_prefix, '.');
                    if (last_dot) strcpy(last_dot + 1, "0/24");
                    if (strcmp(prefix, local_prefix) == 0) {
                        is_own_network = 1;
                        break;
                    }
                }
                if (is_own_network)
                    continue;

                if (route_count < MAX_ROUTES) {
                    strcpy(routing_table[route_count].destination, prefix);
                    strcpy(routing_table[route_count].next_hop, nodes[i].next_hop);
                    strcpy(routing_table[route_count].interface, nodes[i].interface);
                    routing_table[route_count].metrique = nodes[i].distance + topology_db[j].links[k].metrique;
                    routing_table[route_count].nombre_de_saut = (routing_table[route_count].metrique + 999) / 1000;
                    routing_table[route_count].bande_passante = topology_db[j].links[k].bandwidth_mbps;
                    route_count++;
                }
            }
        }
    }
}

void maj_table_routage()
{
    system("ip route flush table 100");

    pthread_mutex_lock(&routing_mutex);
    for (int i = 0; i < route_count; i++)
    {
        char cmd[256];
        if (strcmp(routing_table[i].next_hop, "0.0.0.0") == 0)
            continue;

        snprintf(cmd, sizeof(cmd),
                 "ip route replace %s via %s dev %s",
                 routing_table[i].destination,
                 routing_table[i].next_hop,
                 routing_table[i].interface);
        printf("🛣️  Ajout route OSPF : %s\n", cmd);
        int ret = system(cmd);
        if (ret != 0)
        {
            printf("Erreur lors de l'ajout de la route: %s\n", cmd);
        }
    }
    pthread_mutex_unlock(&routing_mutex);
}