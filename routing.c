#include "routing.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static Route routing_table[MAX_ROUTES];
static int route_count = 0;

void init_routing_table() {
    route_count = 0;
}

// Ajoute ou met à jour une route avec le prochain saut et le nombre de sauts
void add_or_update_route(const char* network, const char* via, int hops) {
    if (hops < 1) return;  // Protection contre valeurs invalides

    for (int i = 0; i < route_count; ++i) {
        if (strncmp(routing_table[i].network, network, sizeof(routing_table[i].network)) == 0) {
            if (routing_table[i].hops <= hops) return;  // Déjà une meilleure route
            strncpy(routing_table[i].next_hop, via, MAX_NAME_LEN - 1);
            routing_table[i].next_hop[MAX_NAME_LEN - 1] = '\0';
            routing_table[i].hops = hops;
            return;
        }
    }

    if (route_count < MAX_ROUTES) {
        strncpy(routing_table[route_count].network, network, sizeof(routing_table[route_count].network) - 1);
        routing_table[route_count].network[sizeof(routing_table[route_count].network) - 1] = '\0';

        strncpy(routing_table[route_count].next_hop, via, MAX_NAME_LEN - 1);
        routing_table[route_count].next_hop[MAX_NAME_LEN - 1] = '\0';

        routing_table[route_count].hops = hops;
        route_count++;
    }
}

// Affiche la table de routage actuelle
void print_routing_table() {
    printf("=== Table de routage ===\n");
    for (int i = 0; i < route_count; ++i) {
        printf("- %s via %s (%d saut%s)\n",
               routing_table[i].network,
               routing_table[i].next_hop,
               routing_table[i].hops,
               routing_table[i].hops > 1 ? "s" : "");
    }
}

// Traite un message de type R2|10.1.0.0/24:1,10.2.0.0/24:2
void process_routing_message(const char* message) {
    char copy[512];
    strncpy(copy, message, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char* sender = strtok(copy, "|");
    char* list = strtok(NULL, "");
    if (!sender || !list) return;

    char* entry = strtok(list, ",");
    while (entry) {
        char net[32];
        int hops;

        if (sscanf(entry, "%31[^:]:%d", net, &hops) == 2) {
            add_or_update_route(net, sender, hops + 1);  // Incrémenter car c'est un saut indirect
        }

        entry = strtok(NULL, ",");
    }
}
