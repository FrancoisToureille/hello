#ifndef ROUTING_H
#define ROUTING_H

void maj_table_routage(void);
void construite_table_routage(noeud_dijkstra_t *nodes, int node_count, int source_index);
void lock_all_mutexes(void);
void unlock_all_mutexes(void);

#endif