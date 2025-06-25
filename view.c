//view.c
#include "types.h"
#include "view.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

void voirVoisins()
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