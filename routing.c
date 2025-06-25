//routing.C
#include "types.h"
#include "routing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
void build_routing_table(dijkstra_node_t *nodes, int node_count, int source_index) {
    route_count = 0;
    // Pour chaque routeur distant (hors soi-même)
    for (int i = 0; i < node_count; i++) {
        if (i == source_index) continue;

        // Trouver le LSA correspondant dans la LSDB
        for (int j = 0; j < topology_db_size; j++) {
            if (strcmp(topology_db[j].router_id, nodes[i].router_id) != 0) continue;

            // Pour chaque interface de ce routeur distant
            for (int k = 0; k < topology_db[j].num_links; k++) {
                const char *dest_ip = topology_db[j].links[k].ip_address;

                // Vérifie que ce n'est pas une de nos propres interfaces
                int is_own_ip = 0;
                for (int m = 0; m < interface_count; m++) {
                    if (strcmp(dest_ip, interfaces[m].ip_address) == 0) {
                        is_own_ip = 1;
                        break;
                    }
                }
                if (is_own_ip)
                    continue;

                // Calcule le préfixe réseau (ex: 10.1.0.0/24)
                char prefix[32];
                strcpy(prefix, dest_ip);
                char *last_dot = strrchr(prefix, '.');
                if (last_dot) strcpy(last_dot + 1, "0/24");

                // Vérifie qu'on n'a pas déjà ajouté cette destination (évite les doublons)
                int already = 0;
                for (int r = 0; r < route_count; r++) {
                    if (strcmp(routing_table[r].destination, prefix) == 0) {
                        already = 1;
                        break;
                    }
                }
                if (already)
                    continue;

                // Vérifie que ce n'est pas un de nos propres réseaux locaux
                int is_own_network = 0;
                for (int m = 0; m < interface_count; m++) {
                    char local_prefix[32];
                    strcpy(local_prefix, interfaces[m].ip_address);
                    char *last_dot = strrchr(local_prefix, '.');
                    if (last_dot) strcpy(last_dot + 1, "0/24");
                    if (strcmp(prefix, local_prefix) == 0) {
                        is_own_network = 1;
                        break;
                    }
                }
                if (is_own_network)
                    continue;

                // Ajoute la route
                if (route_count < MAX_ROUTES) {
                    strcpy(routing_table[route_count].destination, prefix);
                    strcpy(routing_table[route_count].next_hop, nodes[i].next_hop);
                    strcpy(routing_table[route_count].interface, nodes[i].interface);
                    routing_table[route_count].metric = nodes[i].distance + topology_db[j].links[k].metric;
                    routing_table[route_count].hop_count = (routing_table[route_count].metric + 999) / 1000;
                    routing_table[route_count].bandwidth = topology_db[j].links[k].bandwidth_mbps;
                    route_count++;
                }
            }
        }
    }
}

void update_kernel_routing_table()
{
    // Ne supprime que les routes dont le next-hop n'est pas 0.0.0.0 (pas les locales)
    system("ip route flush table 50");
    pthread_mutex_lock(&routing_mutex);
    for (int i = 0; i < route_count; i++)
    {
        char cmd[256];
        if (strcmp(routing_table[i].next_hop, "0.0.0.0") == 0) {
            continue;
        }
        snprintf(cmd, sizeof(cmd),
         "ip route replace %s via %s dev %s",
         routing_table[i].destination,
         routing_table[i].next_hop,
         routing_table[i].interface);
        printf("🛣️  Ajout route OSPF : %s\n", cmd);
        int ret = system(cmd);
        if (ret != 0) {
            printf("Echec de l'ajout de la route: %s\n", cmd);
        }
    }
    pthread_mutex_unlock(&routing_mutex);
}

void supprimerVoisins()
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