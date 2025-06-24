#pragma once

#include <netinet/in.h>  // pour struct in_addr
#include <time.h>
#include "config.h"       // pour MAX_NAME_LEN, INET_ADDRSTRLEN et TIMEOUT_NEIGHBOR

typedef struct {
    char id[MAX_NAME_LEN];              // Identifiant du voisin (ex: R1, R2...)
    char ip[INET_ADDRSTRLEN];           // Adresse IP du voisin
    time_t last_seen;                   // Timestamp du dernier Hello reçu
} Neighbor;

void init_neighbors();
void add_or_update_neighbor(const char* id, const char* ip);
void print_neighbors();
void cleanup_neighbors();
