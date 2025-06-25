#ifndef ROUTING_H
#define ROUTING_H

void update_kernel_routing_table(void);
void build_routing_table(dijkstra_node_t *nodes, int node_count, int source_index);
void lock_all_mutexes(void);
void unlock_all_mutexes(void);

#endif