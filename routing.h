// routing.h
#ifndef ROUTING_H
#define ROUTING_H

#define MAX_ROUTES 128
#define MAX_NAME_LEN 32

typedef struct {
    char network[32];         // Exemple : "10.1.0.0/24"
    char next_hop[MAX_NAME_LEN]; // Exemple : "R2"
    int hops;
} Route;

void init_routing_table();
void add_or_update_route(const char* network, const char* via, int hops);
void print_routing_table();
void process_routing_message(const char* message);

#endif // ROUTING_H
