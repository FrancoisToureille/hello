#ifndef DIJKSTRA_H
#define DIJKSTRA_H

#include "types.h"

// Initialise les nœuds à partir de la base de topologie et des voisins
int initialize_nodes(dijkstra_node_t *nodes);

// Trouve l’index du routeur local (source) dans la table des nœuds
int find_source_index(dijkstra_node_t *nodes, int node_count, const char *hostname);

// Initialise les distances aux voisins directs
void initialize_direct_neighbors(dijkstra_node_t *nodes, int node_count);

// Exécute l’algorithme de Dijkstra
void run_dijkstra(dijkstra_node_t *nodes, int node_count);

// Fonction principale de calcul de plus courts chemins + mise à jour table
void calculate_shortest_paths(void);

#endif // DIJKSTRA_H
