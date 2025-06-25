#include "types.h"
#include "routing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

// Initialise le tableau nodes à partir de la topologie et des voisins
int initialize_nodes(dijkstra_node_t *nodes)
{
    int node_count = 0;

    for (int i = 0; i < taille_topologie; i++)
    {
        strcpy(nodes[node_count].id_routeur, base_topologie[i].id_routeur);
        if (base_topologie[i].nb_liens > 0)
            strcpy(nodes[node_count].adresse_ip, base_topologie[i].liens[0].adresse_ip);
        else
            nodes[node_count].adresse_ip[0] = '\0';

        nodes[node_count].distance = INT_MAX;
        nodes[node_count].prochain_saut[0] = '\0';
        nodes[node_count].interface[0] = '\0';
        nodes[node_count].visite = 0;
        nodes[node_count].debit = 0;

        node_count++;
    }

    // Ajouter voisins directs absents de la topologie
    for (int i = 0; i < nombre_voisins; i++)
    {
        if (voisins[i].etat_lien == 1)
        {
            int found = 0;
            for (int j = 0; j < node_count; j++)
            {
                if (strcmp(nodes[j].id_routeur, voisins[i].id_routeur) == 0)
                {
                    found = 1;
                    break;
                }
            }
            if (!found && node_count < NB_MAX_VOISINS)
            {
                strcpy(nodes[node_count].id_routeur, voisins[i].id_routeur);
                strcpy(nodes[node_count].adresse_ip, voisins[i].adresse_ip);
                nodes[node_count].distance = INT_MAX;
                nodes[node_count].prochain_saut[0] = '\0';
                nodes[node_count].interface[0] = '\0';
                nodes[node_count].visite = 0;
                nodes[node_count].debit = 0;

                node_count++;
            }
        }
    }

    return node_count;
}

// Trouve l'index du noeud source dans nodes par nom d'hôte
int find_source_index(dijkstra_node_t *nodes, int node_count, const char *hostname)
{
    for (int i = 0; i < node_count; i++)
    {
        if (strcmp(nodes[i].id_routeur, hostname) == 0)
            return i;
    }
    return -1;
}

// Initialise les distances des voisins directs
void initialize_direct_neighbors(dijkstra_node_t *nodes, int node_count)
{
    for (int i = 0; i < nombre_voisins; i++)
    {
        if (voisins[i].etat_lien == 1)
        {
            for (int j = 0; j < node_count; j++)
            {
                if (strcmp(nodes[j].id_routeur, voisins[i].id_routeur) == 0)
                {
                    int metric = voisins[i].metrique + (1000 / (voisins[i].debit_mbps > 0 ? voisins[i].debit_mbps : 1));
                    nodes[j].distance = metric;
                    strcpy(nodes[j].prochain_saut, voisins[i].adresse_ip);
                    strcpy(nodes[j].interface, voisins[i].interface);
                    nodes[j].debit = voisins[i].debit_mbps;
                    break;
                }
            }
        }
    }
}

// Exécute l'algorithme de Dijkstra
void run_dijkstra(dijkstra_node_t *nodes, int node_count)
{
    for (int count = 0; count < node_count - 1; count++)
    {
        int min_distance = INT_MAX;
        int min_index = -1;

        for (int i = 0; i < node_count; i++)
        {
            if (!nodes[i].visite && nodes[i].distance < min_distance)
            {
                min_distance = nodes[i].distance;
                min_index = i;
            }
        }

        if (min_index == -1)
            break;

        nodes[min_index].visite = 1;

        // Chercher LSA correspondant
        lsa_t *current_lsa = NULL;
        for (int i = 0; i < taille_topologie; i++)
        {
            if (strcmp(base_topologie[i].id_routeur, nodes[min_index].id_routeur) == 0)
            {
                current_lsa = &base_topologie[i];
                break;
            }
        }
        if (!current_lsa)
            continue;

        for (int i = 0; i < current_lsa->nb_liens; i++)
        {
            int neighbor_index = -1;
            for (int j = 0; j < node_count; j++)
            {
                if (strcmp(nodes[j].id_routeur, current_lsa->liens[i].id_routeur) == 0)
                {
                    neighbor_index = j;
                    break;
                }
            }

            if (neighbor_index >= 0 && !nodes[neighbor_index].visite)
            {
                int link_cost = current_lsa->liens[i].metrique + (1000 / (current_lsa->liens[i].debit_mbps > 0 ? current_lsa->liens[i].debit_mbps : 1));
                int new_distance = nodes[min_index].distance + link_cost;

                if (new_distance < nodes[neighbor_index].distance)
                {
                    nodes[neighbor_index].distance = new_distance;

                    if (nodes[min_index].distance == 0)
                    {
                        strcpy(nodes[neighbor_index].prochain_saut, current_lsa->liens[i].adresse_ip);
                        strcpy(nodes[neighbor_index].interface, current_lsa->liens[i].interface);
                        nodes[neighbor_index].debit = current_lsa->liens[i].debit_mbps;
                    }
                    else
                    {
                        strcpy(nodes[neighbor_index].prochain_saut, nodes[min_index].prochain_saut);
                        strcpy(nodes[neighbor_index].interface, nodes[min_index].interface);
                        nodes[neighbor_index].debit = nodes[min_index].debit;
                    }
                }
            }
        }
    }
}

// Fonction principale pour calculer les plus courts chemins
void calculate_shortest_paths()
{
    printf("🔧 DEBUG: Début calcul des chemins\n");

    // tu dois définir et implémenter lock_all_mutexes() et unlock_all_mutexes() pour protéger base_topologie, voisins, table_routage etc.
    lock_all_mutexes();

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    nombre_routes = 0;

    dijkstra_node_t nodes[NB_MAX_VOISINS];
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

    // build_routing_table() à implémenter : construit table_routage à partir des nodes calculés
    build_routing_table(nodes, node_count, source_index);

    unlock_all_mutexes();

    // update_kernel_routing_table() à implémenter : met à jour la table de routage système
    update_kernel_routing_table();

    printf("🗺️  Table de routage mise à jour (%d routes calculées avec Dijkstra)\n", nombre_routes);
}
