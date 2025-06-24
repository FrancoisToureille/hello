//dijkstra.c
#include "types.h"
#include "routing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
int initialize_nodes(dijkstra_node_t *nodes)
{
    int node_count = 0;

    for (int i = 0; i < topology_db_size; i++)
    {
        strcpy(nodes[node_count].router_id, topology_db[i].router_id);
        if (topology_db[i].num_links > 0)
        {
            strcpy(nodes[node_count].ip_address, topology_db[i].links[0].ip_address);
        }
        else
        {
            nodes[node_count].ip_address[0] = '\0';
        }
        nodes[node_count].distance = INT_MAX;
        nodes[node_count].next_hop[0] = '\0';
        nodes[node_count].interface[0] = '\0';
        nodes[node_count].visited = 0;
        nodes[node_count].bandwidth = 0;
        node_count++;
    }

    for (int i = 0; i < neighbor_count; i++)
    {
        if (neighbors[i].link_state == 1)
        {
            int found = 0;
            for (int j = 0; j < node_count; j++)
            {
                if (strcmp(nodes[j].router_id, neighbors[i].router_id) == 0)
                {
                    found = 1;
                    break;
                }
            }
            if (!found && node_count < MAX_NEIGHBORS)
            {
                strcpy(nodes[node_count].router_id, neighbors[i].router_id);
                strcpy(nodes[node_count].ip_address, neighbors[i].ip_address);
                nodes[node_count].distance = INT_MAX;
                nodes[node_count].next_hop[0] = '\0';
                nodes[node_count].interface[0] = '\0';
                nodes[node_count].visited = 0;
                nodes[node_count].bandwidth = 0;
                node_count++;
            }
        }
    }

    return node_count;
}

int find_source_index(dijkstra_node_t *nodes, int node_count, const char *hostname)
{
    for (int i = 0; i < node_count; i++)
    {
        if (strcmp(nodes[i].router_id, hostname) == 0)
        {
            return i;
        }
    }
    return -1;
}

void initialize_direct_neighbors(dijkstra_node_t *nodes, int node_count)
{
    for (int i = 0; i < neighbor_count; i++)
    {
        if (neighbors[i].link_state == 1)
        {
            for (int j = 0; j < node_count; j++)
            {
                if (strcmp(nodes[j].router_id, neighbors[i].router_id) == 0)
                {
                    int metric = neighbors[i].metric + (1000 / neighbors[i].bandwidth_mbps);
                    nodes[j].distance = metric;
                    strcpy(nodes[j].next_hop, neighbors[i].ip_address);
                    strcpy(nodes[j].interface, neighbors[i].interface);
                    nodes[j].bandwidth = neighbors[i].bandwidth_mbps;
                    break;
                }
            }
        }
    }
}

void run_dijkstra(dijkstra_node_t *nodes, int node_count)
{
    for (int count = 0; count < node_count - 1; count++)
    {
        int min_distance = INT_MAX;
        int min_index = -1;

        for (int i = 0; i < node_count; i++)
        {
            if (!nodes[i].visited && nodes[i].distance < min_distance)
            {
                min_distance = nodes[i].distance;
                min_index = i;
            }
        }

        if (min_index == -1)
            break;

        nodes[min_index].visited = 1;

        lsa_t *current_lsa = NULL;
        for (int i = 0; i < topology_db_size; i++)
        {
            if (strcmp(topology_db[i].router_id, nodes[min_index].router_id) == 0)
            {
                current_lsa = &topology_db[i];
                break;
            }
        }

        if (!current_lsa)
            continue;

        for (int i = 0; i < current_lsa->num_links; i++)
        {
            int neighbor_index = -1;
            for (int j = 0; j < node_count; j++)
            {
                if (strcmp(nodes[j].router_id, current_lsa->links[i].router_id) == 0)
                {
                    neighbor_index = j; 
                    break;
                }
            }

            if (neighbor_index >= 0 && !nodes[neighbor_index].visited)
            {
                int link_cost = current_lsa->links[i].metric + (1000 / current_lsa->links[i].bandwidth_mbps);
                int new_distance = nodes[min_index].distance + link_cost;

                if (new_distance < nodes[neighbor_index].distance)
                {
                    nodes[neighbor_index].distance = new_distance;

                    if (nodes[min_index].distance == 0)
                    {
                        strcpy(nodes[neighbor_index].next_hop, current_lsa->links[i].ip_address);
                        strcpy(nodes[neighbor_index].interface, current_lsa->links[i].interface);
                        nodes[neighbor_index].bandwidth = current_lsa->links[i].bandwidth_mbps;
                    }
                    else
                    {
                        strcpy(nodes[neighbor_index].next_hop, nodes[min_index].next_hop);
                        strcpy(nodes[neighbor_index].interface, nodes[min_index].interface);
                        nodes[neighbor_index].bandwidth = nodes[min_index].bandwidth;
                    }
                }
            }
        }
    }
}

// Fonction principale refactorisée
void calculate_shortest_paths()
{
    printf("🔧 DEBUG: Début calcul des chemins\n");
    lock_all_mutexes();

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    route_count = 0;

    dijkstra_node_t nodes[MAX_NEIGHBORS];
    int node_count = initialize_nodes(nodes);

    int source_index = find_source_index(nodes, node_count, hostname);
    if (source_index < 0)
    {
        unlock_all_mutexes();
        return;
    }

    nodes[source_index].distance = 0;
    initialize_direct_neighbors(nodes, node_count);
    run_dijkstra(nodes, node_count);
    build_routing_table(nodes, node_count, source_index);

    unlock_all_mutexes();

    update_kernel_routing_table();
    printf("🗺️  Table de routage mise à jour (%d routes calculées avec Dijkstra)\n", route_count);
}