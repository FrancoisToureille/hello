// types.h
#pragma once

#include <pthread.h>   // ✅ Pour pthread_mutex_t
#include <time.h>      // ✅ Pour time_t

// Constantes
#define BROADCAST_PORT 9090
#define BUFFER_SIZE 1024
#define BROADCAST_IP "10.1.0.255"
#define MAX_NEIGHBORS 10
#define MAX_INTERFACES 5
#define HELLO_INTERVAL 15
#define NEIGHBOR_TIMEOUT 30
#define MAX_ROUTES 50

// Structures
typedef struct {
    char router_id[32];
    char ip_address[16];
    char interface[16];
    int metric;
    time_t last_hello;
    int bandwidth_mbps;
    int link_state;
} neighbor_t;

typedef struct {
    char name[16];
    char ip_address[16];
    char broadcast_ip[16];
    int is_active;
} interface_t;

typedef struct {
    char destination[16];
    char next_hop[16];
    char interface[16];
    int metric;
    int hop_count;
    int bandwidth;
} route_t;

typedef struct {
    char router_id[32];
    int sequence_number;
    time_t timestamp;
    int num_links;
    neighbor_t links[MAX_NEIGHBORS];
} lsa_t;

typedef struct {
    char router_id[32];
    char ip_address[16];
    int distance;
    char next_hop[16];
    char interface[16];
    int visited;
    int bandwidth;
} dijkstra_node_t;

// Variables globales (extern)
extern neighbor_t neighbors[MAX_NEIGHBORS];
extern int neighbor_count;
extern pthread_mutex_t neighbor_mutex;

extern interface_t interfaces[MAX_INTERFACES];
extern int interface_count;

extern route_t routing_table[MAX_ROUTES];
extern int route_count;
extern pthread_mutex_t routing_mutex;

extern lsa_t topology_db[MAX_NEIGHBORS];
extern int topology_db_size;
extern pthread_mutex_t topology_mutex;

extern dijkstra_node_t nodes[MAX_NEIGHBORS];
extern int node_count;

extern int broadcast_sock;
extern int listen_sock;
extern volatile int running;
