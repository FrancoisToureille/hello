#include "neighbors.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static Neighbor neighbors[MAX_NEIGHBORS];
static int count = 0;

void init_neighbors() {
    count = 0;
}

void add_or_update_neighbor(const char* id, const char* ip) {
    time_t now = time(NULL);
    for (int i = 0; i < count; ++i) {
        if (strcmp(neighbors[i].id, id) == 0) {
            neighbors[i].last_seen = now;
            return;
        }
    }
    if (count < MAX_NEIGHBORS) {
        strncpy(neighbors[count].id, id, MAX_NAME_LEN);
        strncpy(neighbors[count].ip, ip, INET_ADDRSTRLEN);
        neighbors[count].last_seen = now;
        printf("[+] Nouveau voisin : %s (%s)\n", id, ip);
        count++;
    }
}

void print_neighbors() {
    printf("=== Voisins connus ===\n");
    for (int i = 0; i < count; ++i) {
        printf("- %s (%s)\n", neighbors[i].id, neighbors[i].ip);
    }
}

void cleanup_neighbors() {
    time_t now = time(NULL);
    int i = 0;
    while (i < count) {
        if (now - neighbors[i].last_seen > TIMEOUT_NEIGHBOR) {
            printf("[-] Voisin perdu : %s\n", neighbors[i].id);
            neighbors[i] = neighbors[--count]; // Écraser par le dernier
        } else {
            i++;
        }
    }
}