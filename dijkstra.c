#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

#include "types.h"
#include "routing.h"

int initializer_noeuds_dijkstra(noeud_dijkstra_t *nodes)
{
    int node_count = 0;

    for (int i = 0; i < topology_db_size; i++)
    {
        strcpy(nodes[node_count].id_routeur, topology_db[i].id_routeur);
        if (topology_db[i].num_links > 0)
        {
            strcpy(nodes[node_count].adresse_ip, topology_db[i].links[0].adresse_ip);
        }
        else
        {
            nodes[node_count].adresse_ip[0] = '\0';
        }
        nodes[node_count].distance = INT_MAX;
        nodes[node_count].next_hop[0] = '\0';
        nodes[node_count].interface[0] = '\0';
        nodes[node_count].visited = 0;
        nodes[node_count].bande_passante = 0;
        node_count++;
    }

    for (int i = 0; i < neighbor_count; i++)
    {
        if (neighbors[i].etat_lien == 1)
        {
            int found = 0;
            for (int j = 0; j < node_count; j++)
            {
                if (strcmp(nodes[j].id_routeur, neighbors[i].id_routeur) == 0)
                {
                    found = 1;
                    break;
                }
            }
            if (!found && node_count < MAX_NEIGHBORS)
            {
                strcpy(nodes[node_count].id_routeur, neighbors[i].id_routeur);
                strcpy(nodes[node_count].adresse_ip, neighbors[i].adresse_ip);
                nodes[node_count].distance = INT_MAX;
                nodes[node_count].next_hop[0] = '\0';
                nodes[node_count].interface[0] = '\0';
                nodes[node_count].visited = 0;
                nodes[node_count].bande_passante = 0;
                node_count++;
            }
        }
    }

    return node_count;
}
int trouver_racine(noeud_dijkstra_t *nodes, int node_count, const char *hostname)
{
    for (int i = 0; i < node_count; i++)
    {
        if (strcmp(nodes[i].id_routeur, hostname) == 0)
        {
            return i;
        }
    }
    return -1;
}


void initialiser_voisins(noeud_dijkstra_t *nodes, int node_count)
{
    for (int i = 0; i < neighbor_count; i++)
    {
        if (neighbors[i].etat_lien == 1)
        {
            for (int j = 0; j < node_count; j++)
            {
                if (strcmp(nodes[j].id_routeur, neighbors[i].id_routeur) == 0)
                {
                    int metrique = neighbors[i].metrique + (1000 / neighbors[i].bandwidth_mbps);
                    nodes[j].distance = metrique;
                    strcpy(nodes[j].next_hop, neighbors[i].adresse_ip);
                    strcpy(nodes[j].interface, neighbors[i].interface);
                    nodes[j].bande_passante = neighbors[i].bandwidth_mbps;
                    break;
                }
            }
        }
    }
}


void dijkstra(noeud_dijkstra_t *nodes, int node_count)
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
            if (strcmp(topology_db[i].id_routeur, nodes[min_index].id_routeur) == 0)
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
                if (strcmp(nodes[j].id_routeur, current_lsa->links[i].id_routeur) == 0)
                {
                    neighbor_index = j; 
                    break;
                }
            }

            if (neighbor_index >= 0 && !nodes[neighbor_index].visited)
            {
                int link_cost = current_lsa->links[i].metrique + (1000 / current_lsa->links[i].bandwidth_mbps);
                int new_distance = nodes[min_index].distance + link_cost;

                if (new_distance < nodes[neighbor_index].distance)
                {
                    nodes[neighbor_index].distance = new_distance;

                    if (nodes[min_index].distance == 0)
                    {
                        strcpy(nodes[neighbor_index].next_hop, current_lsa->links[i].adresse_ip);
                        strcpy(nodes[neighbor_index].interface, current_lsa->links[i].interface);
                        nodes[neighbor_index].bande_passante = current_lsa->links[i].bandwidth_mbps;
                    }
                    else
                    {
                        strcpy(nodes[neighbor_index].next_hop, nodes[min_index].next_hop);
                        strcpy(nodes[neighbor_index].interface, nodes[min_index].interface);
                        nodes[neighbor_index].bande_passante = nodes[min_index].bande_passante;
                    }
                }
            }
        }
    }
}

void calcul_chemins()
{
    lock_all_mutexes();
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }
    route_count = 0;

    noeud_dijkstra_t nodes[MAX_NEIGHBORS];
    int node_count = initializer_noeuds_dijkstra(nodes);

    int source_index = trouver_racine(nodes, node_count, hostname);
    if (source_index < 0)
    {
        unlock_all_mutexes();
        return;
    }

    nodes[source_index].distance = 0;
    initialiser_voisins(nodes, node_count);
    dijkstra(nodes, node_count);
    construite_table_routage(nodes, node_count, source_index);

    unlock_all_mutexes();

    maj_table_routage();
    printf("Table de routage mise à jour\n");
}