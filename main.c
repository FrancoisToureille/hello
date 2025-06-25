// main.c
#include <stdio.h>      // printf, perror, fflush, stdin, stdout
#include "types.h"
#include "view.h"
#include "hello.h"
#include "lsa.h"
#include "routing.h"
#include "control.h"
#include <stdlib.h>     // system, exit
#include <string.h>     // strcpy, strcmp, strcspn, strlen
#include <unistd.h>     // close, sleep, gethostname
#include <signal.h>     // signal, SIGINT, SIGTERM

// Variables globales
voisin_t voisins[NB_MAX_VOISINS];
interface_reseau_t interfaces_reseau[NB_MAX_INTERFACES];
route_t table_routage[NB_MAX_ROUTES];
lsa_t base_topologie[NB_MAX_VOISINS];
dijkstra_node_t noeuds_dijkstra[NB_MAX_VOISINS];

int nombre_voisins = 0;
int nombre_interfaces = 0;
int nombre_routes = 0;
int taille_topologie = 0;
int nombre_noeuds = 0;
int socket_broadcast = -1;
int socket_ecoute = -1;
volatile int en_fonctionnement = 1;

// Mutex
pthread_mutex_t mutex_voisins = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_routage = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_topologie = PTHREAD_MUTEX_INITIALIZER;

// Prototypes
void mettre_a_jour_table_kernel(void);
void gerer_message_hello(const char *message, const char *ip_expediteur);
void gerer_message_lsa(const char *message, const char *ip_expediteur);
void diffuser_lsa(const char *message_lsa);
void gestion_signal(int signal);

int main(int argc, char *argv[])
{
    pthread_t thread_ecoute, thread_hello, thread_lsa;
    char entree[TAILLE_BUFFER];
    char nom_hote[256];

    socket_broadcast = -1;
    socket_ecoute = -1;

    signal(SIGINT, gestion_signal);
    signal(SIGTERM, gestion_signal);

    if (gethostname(nom_hote, sizeof(nom_hote)) != 0) {
        perror("Erreur récupération nom hôte");
        strcpy(nom_hote, "Inconnu");
    }

    printf("Routeur actif : %s\n", nom_hote);
    printf("=====================================\n\n");
    printf("Détection des interfaces réseau locales...\n");
    if (discover_interfaces() <= 0)
    {
        fprintf(stderr, "Aucune interface réseau détectée, arrêt.\n");
        return 1;
    }

    ensure_local_routes();

    // Étape 2 : Création du socket broadcast
    socket_broadcast = create_broadcast_socket();
    if (socket_broadcast < 0)
    {
        fprintf(stderr, "Échec création socket broadcast.\n");
        return 1;
    }

    if (pthread_create(&thread_ecoute, NULL, listen_thread, NULL) != 0)
    {
        perror("Erreur création thread d'écoute");
        close(socket_broadcast);
        return 1;
    }

    if (pthread_create(&thread_hello, NULL, thread_hello, NULL) != 0)
    {
        perror("Erreur création thread Hello");
        close(socket_broadcast);
        return 1;
    }

    if (pthread_create(&thread_lsa, NULL, lsa_thread, NULL) != 0)
    {
        perror("Erreur création thread LSA");
        close(socket_broadcast);
        return 1;
    }

    // Pause pour laisser démarrer tous les services
    sleep(5);

    // Initialisation du LSA local dans la base
    initialize_own_lsa();

    printf("💬 Commandes disponibles: 'voisins', 'routes', 'stop'\n");

    while (en_fonctionnement)
    {
        printf("💬 Saisissez une commande: ");
        fflush(stdout);

        if (fgets(entree, sizeof(entree), stdin) == NULL)
        {
            break;
        }

        entree[strcspn(entree, "\n")] = 0;

        if (strcmp(entree, "voisins") == 0) {
            voirVoisins();
        } else if (strcmp(entree, "stop") == 0) {
            system("ip route flush table 100");
            break;
        } else if (strcmp(entree, "routes") == 0) {
            printf("🔄 Calcul des routes en cours...\n");
            struct timespec delai;
            clock_gettime(CLOCK_REALTIME, &delai);
            delai.tv_sec += 3; // Timeout 3 secondes

            if (pthread_mutex_timedlock(&mutex_routage, &delai) == 0)
            {
                show_routing_table();
                pthread_mutex_unlock(&mutex_routage);
            }
            else
            {
                printf("⚠️  Aucune route disponible pour le moment.\n");
            }
        } else if (strlen(entree) > 0) {
            send_message(entree);
        }
    }

    en_fonctionnement = 0;

    // Fermeture des sockets pour débloquer les threads
    if (socket_broadcast >= 0)
    {
        close(socket_broadcast);
        socket_broadcast = -1;
    }
    if (socket_ecoute >= 0)
    {
        close(socket_ecoute);
        socket_ecoute = -1;
    }

    struct timespec timeout_thread;
    timeout_thread.tv_sec = 2;
    timeout_thread.tv_nsec = 0;

    printf("🛑 Arrêt du routeur.\n");

    join_or_cancel(thread_ecoute, "d'écoute", &timeout_thread);
    join_or_cancel(thread_hello, "Hello", &timeout_thread);
    join_or_cancel(thread_lsa, "LSA", &timeout_thread);

    return 0;
}