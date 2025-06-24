#pragma once
void update_kernel_routing_table(void);
void build_routing_table(dijkstra_node_t *nodes, int node_count, int source_index);
void calculate_shortest_paths(void);
