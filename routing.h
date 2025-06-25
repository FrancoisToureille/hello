#pragma once
void supprimer_voisins_expires(void);
void construire_table_routage(dijkstra_node_t *nodes, int node_count, int source_index);
void mettre_a_jour_table_kernel(void);
