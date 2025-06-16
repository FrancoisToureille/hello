#pragma once
#include <time.h>

typedef struct {
    char id[MAX_NAME_LEN];
    char ip[INET_ADDRSTRLEN];
    time_t last_seen;
} Neighbor;

void init_neighbors();
void add_or_update_neighbor(const char* id, const char* ip);
void print_neighbors();
