#ifndef DIJKSTRA_H
#define DIJKSTRA_H

#include "types.h"

int initializer_noeuds_dijkstra(noeud_dijkstra_t *nodes);

int trouver_racine(noeud_dijkstra_t *nodes, int node_count, const char *hostname);

void initialiser_voisins(noeud_dijkstra_t *nodes, int node_count);

void dijkstra(noeud_dijkstra_t *nodes, int node_count);

void calcul_chemins(void);

#endif 
