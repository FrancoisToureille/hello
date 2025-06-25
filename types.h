#pragma once

#include <pthread.h>   // ✅ Pour pthread_mutex_t
#include <time.h>      // ✅ Pour time_t

// Constantes
#define PORT_DIFFUSION 9191
#define TAILLE_BUFFER 2048
#define IP_DIFFUSION "10.1.0.255"
#define NB_MAX_VOISINS 10
#define NB_MAX_INTERFACES 5
#define INTERVALLE_HELLO 20
#define DELAI_EXPIRATION_VOISIN 30
#define NB_MAX_ROUTES 50

// Structures
typedef struct {
    char id_routeur[32];
    char adresse_ip[16];
    char interface[16];
    int metrique;
    time_t dernier_hello;
    int bande_passante_mbps;
    int etat_lien;
} voisin_t;

typedef struct {
    char nom[16];
    char adresse_ip[16];
    char ip_diffusion[16];
    int actif;
} interface_reseau_t;

typedef struct {
    char destination[16];
    char prochain_saut[16];
    char interface[16];
    int metrique;
    int nombre_sauts;
    int bande_passante;
} route_t;

typedef struct {
    char id_routeur[32];
    int numero_sequence;
    time_t horodatage;
    int nb_liens;
    voisin_t liens[NB_MAX_VOISINS];
} lsa_t;

typedef struct {
    char id_routeur[32];
    int distance;
    char prochain_saut[16];
    char interface[16];
    int visite;
    int bande_passante;
} noeud_dijkstra_t;

// Variables globales (extern)
extern voisin_t voisins[NB_MAX_VOISINS];
extern int nombre_voisins;
extern pthread_mutex_t mutex_voisins;

extern interface_reseau_t interfaces[NB_MAX_INTERFACES];
extern int nombre_interfaces;

extern route_t table_routage[NB_MAX_ROUTES];
extern int nombre_routes;
extern pthread_mutex_t mutex_routage;

extern lsa_t base_topologie[NB_MAX_VOISINS];
extern int taille_base_topologie;
extern pthread_mutex_t mutex_topologie;

extern noeud_dijkstra_t noeuds[NB_MAX_VOISINS];
extern int nombre_noeuds;

extern int socket_diffusion;
extern int socket_ecoute;
extern volatile int en_fonctionnement;
