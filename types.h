#ifndef TYPES_H
#define TYPES_H

#include <time.h>
#include <pthread.h>

#define BROADCAST_PORT 5000
#define LEN_BUFFER 1024

#define MAX_NEIGHBORS 10
#define MAX_INTERFACES 5
#define MAX_ROUTES 50

#define HELLO_INTERVAL 10
#define LIMITE_VOISIN 30
#define IP_BROADCAST "10.1.0.255"

// ===== STRUCTURES =====

typedef struct {
    char id_routeur[32];
    char adresse_ip[16];
    char interface[16];
    int metrique;
    time_t dernier_hello;
    int bandwidth_mbps;
    int etat_lien; 
} voisin_t;

typedef struct {
    char nom[16];
    char adresse_ip[16];
    char ip_diffusion[16];
    int active;
} interface_t;

typedef struct {
    char destination[16];
    char next_hop[16];
    char interface[16];
    int metrique;
    int nombre_de_saut;
    int bande_passante;
} route_t;

typedef struct {
    char id_routeur[32];
    int sequence_number;
    time_t timestamp;
    int num_links;
    voisin_t links[MAX_NEIGHBORS];
} lsa_t;

typedef struct {
    char id_routeur[32];
    char adresse_ip[16];
    int distance;
    char next_hop[16];
    char interface[16];
    int visited;
    int bande_passante;
} noeud_dijkstra_t;

// ===== VARIABLES GLOBALES (extern) =====

extern voisin_t neighbors[MAX_NEIGHBORS];
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

extern noeud_dijkstra_t nodes[MAX_NEIGHBORS];
extern int node_count;

extern volatile int running;
extern int broadcast_sock;
extern int listen_sock;

#endif // TYPES_H