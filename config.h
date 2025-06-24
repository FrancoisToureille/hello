#pragma once

#include <netinet/in.h>  // Pour INET_ADDRSTRLEN

// Constantes de configuration
#define MAX_INTERFACES 10
#define MAX_NAME_LEN 32
#define MAX_NEIGHBORS 32
#define MAX_ROUTES 64

// Ports centralisés ici
#define HELLO_PORT 5000
#define ROUTING_PORT 5001
#define CONTROL_PORT 9090

// Périodes (en secondes)
#define HELLO_INTERVAL 5
#define TIMEOUT_NEIGHBOR 15

// Données globales du routeur
extern char router_id[MAX_NAME_LEN];
extern char interfaces[MAX_INTERFACES][MAX_NAME_LEN];
extern int interface_count;
extern char broadcasts[MAX_INTERFACES][INET_ADDRSTRLEN];

// Chargement du fichier de configuration
void load_config(const char* filename);
