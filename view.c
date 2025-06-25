#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "types.h"
#include "view.h"


void print_usage(char *program_name)
{
    printf("Usage: %s\n", program_name);
    printf("Le programme démarre automatiquement en mode écoute.\n");
    printf("Tapez vos messages et appuyez sur Entrée pour les envoyer.\n");
    printf("Tapez 'quit' ou 'exit' pour quitter.\n");
}

// Fonction pour afficher la table de routage
void show_routing_table()
{

    printf("\n=== Table de routage ===\n");
    printf("%-15s %-15s %-12s %-8s %-5s\n", "Destination", "Next Hop", "Interface", "Métrique", "Sauts");
    printf("---------------------------------------------------------------\n");

    for (int i = 0; i < route_count; i++)
    {
        printf("%-15s %-15s %-12s %-8d %-5d\n",
               routing_table[i].destination,
               routing_table[i].next_hop,
               routing_table[i].interface,
               routing_table[i].metric,
               routing_table[i].hop_count);
    }
}

// Function to display the network topology
void show_topology(void)
{

    printf("\n=== Topologie du réseau ===\n");
    printf("%-15s %-8s %-10s %-15s\n", "Routeur", "Séquence", "Liens", "Voisins");
    printf("--------------------------------------------------------\n");

    for (int i = 0; i < topology_db_size; i++)
    {
        printf("%-15s %-8d %-10d ",
               topology_db[i].router_id,
               topology_db[i].sequence_number,
               topology_db[i].num_links);

        // Print first neighbor
        int first_printed = 0;
        for (int j = 0; j < topology_db[i].num_links; j++)
        {
            // Ne pas afficher le routeur lui-même
            if (strcmp(topology_db[i].links[j].router_id, topology_db[i].router_id) != 0)
            {
                if (first_printed)
                {
                    printf(", ");
                }
                printf("%s", topology_db[i].links[j].router_id);
                first_printed = 1;
            }
        }
        printf("\n");
    }
}
void debug_topology_db()
{
    pthread_mutex_lock(&topology_mutex);
    printf("\n=== DEBUG: Base de données topologique ===\n");
    for (int i = 0; i < topology_db_size; i++)
    {
        printf("Routeur %s: %d liens\n", topology_db[i].router_id, topology_db[i].num_links);
        for (int j = 0; j < topology_db[i].num_links; j++)
        {
            printf("  -> %s (%s)\n",
                   topology_db[i].links[j].router_id,
                   topology_db[i].links[j].ip_address);
        }
    }
    pthread_mutex_unlock(&topology_mutex);
}

void check_system_status()
{
    printf("=== État du système ===\n");
    printf("Running: %s\n", running ? "OUI" : "NON");
    printf("Voisins: %d\n", neighbor_count);
    printf("Topologie DB: %d entrées\n", topology_db_size);
    printf("Routes: %d\n", route_count);
    printf("Interfaces: %d\n", interface_count);
    printf("======================\n");
}
// Fonction pour afficher la table des voisins
void show_neighbors()
{
    pthread_mutex_lock(&neighbor_mutex);

    printf("\n=== Table des voisins ===\n");
    printf("%-15s %-15s %-8s %-10s %-8s\n", "Routeur", "IP", "Métrique", "Bande Pass.", "État");
    printf("--------------------------------------------------------\n");

    for (int i = 0; i < neighbor_count; i++)
    {
        printf("%-15s %-15s %-8d %-10d %-8s\n",
               neighbors[i].router_id,
               neighbors[i].ip_address,
               neighbors[i].metric,
               neighbors[i].bandwidth_mbps,
               neighbors[i].link_state ? "UP" : "DOWN");
    }

    pthread_mutex_unlock(&neighbor_mutex);
}