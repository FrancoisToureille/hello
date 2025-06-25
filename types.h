// Nouveau fichier types.h corrigé pour réconcilier tous les types et constantes manquants
#ifndef TYPES_H
#define TYPES_H

#include <pthread.h>
#include <time.h>

#define NB_MAX_VOISINS 100
#define NB_MAX_INTERFACES 10
#define NB_MAX_ROUTES 100
#define NB_MAX_TOPOLOGIE 100
#define TAILLE_BUFFER 1024
#define INTERVALLE_HELLO 5
#define PORT_DIFFUSION 5000
#define DELAI_EXPIRATION_VOISINS 20

// Structures
typedef struct {
    char nom[32];
    char ip_locale[32];
    char ip_diffusion[32];
    int active;
} interface_reseau_t;

typedef struct {
    char id_routeur[32];
    char adresse_ip[32];
    char interface[32];
    int metrique;
    int debit_mbps;
    int etat_lien;
    time_t dernier_hello;
} voisin_t;

typedef struct {
    char destination[32];
    char prochain_saut[32];
    char interface[32];
    int metrique;
    int nombre_sauts;
    int debit;
} route_t;

typedef struct {
    char id_routeur[32];
    int horodatage;
    int nb_liens;
    voisin_t liens[NB_MAX_VOISINS];
} lsa_t;

typedef struct {
    char id_routeur[32];
    char adresse_ip[32];
    int visite;
    int distance;
    char prochain_saut[32];
    char interface[32];
    int debit;
} dijkstra_node_t;

// Variables globales externes
extern voisin_t voisins[NB_MAX_VOISINS];
extern int nombre_voisins;
extern pthread_mutex_t mutex_voisins;

extern interface_reseau_t interfaces[NB_MAX_INTERFACES];
extern int nombre_interfaces;
extern pthread_mutex_t mutex_interfaces;

extern route_t table_routage[NB_MAX_ROUTES];
extern int nombre_routes;
extern pthread_mutex_t mutex_routage;

extern lsa_t base_topologie[NB_MAX_TOPOLOGIE];
extern int taille_topologie;
extern pthread_mutex_t mutex_topologie;

extern int socket_diffusion;
extern int socket_ecoute;
extern volatile int en_fonctionnement;

#endif // TYPES_H