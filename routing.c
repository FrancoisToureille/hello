#include "routing.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static Route routing_table[MAX_ROUTES];
static int route_count = 0;

void init_routing_table() {
    route_count = 0;
}

void add_or_update_route(const char* network, const char* via, int hops) {
    for (int i = 0; i < route_count; ++i) {
        if (strcmp(routing_table[i].network, network) == 0) {
            if (routing_table[i].hops <= hops) return;
            strncpy(routing_table[i].next_hop, via, MAX_NAME_LEN);
            routing_table[i].hops = hops;
            return;
        }
    }
    if (route_count < MAX_ROUTES) {
        strncpy(routing_table[route_count].network, network, sizeof(routing_table[route_count].network));
        strncpy(routing_table[route_count].next_hop, via, MAX_NAME_LEN);
        routing_table[route_count].hops = hops;
        route_count++;
    }
}

void print_routing_table() {
    printf("=== Table de routage ===\\n");
    for (int i = 0; i < route_count; ++i) {
        printf("- %s via %s (%d saut(s))\\n", routing_table[i].network,
               routing_table[i].next_hop, routing_table[i].hops);
    }
}

void process_routing_message(const char* message, const char* sender_ip) {
    // Format attendu : R2|10.1.0.0/24:1,10.2.0.0/24:2
    char copy[512];
    strncpy(copy, message, sizeof(copy));
    copy[sizeof(copy) - 1] = '\\0';

    char* sender_id = strtok(copy, "|");
    char* list = strtok(NULL, "");
    if (!sender_id || !list) return;

    char* entry = strtok(list, ",");
    while (entry) {
        char net[32];
        int hops;
        if (sscanf(entry, "%31[^:]:%d", net, &hops) == 2) {
            add_or_update_route(net, sender_ip, hops + 1);  // utilise l'adresse IP comme passerelle
        }
        entry = strtok(NULL, ",");
    }
}