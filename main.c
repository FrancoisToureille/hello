#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

#include "types.h"
#include "control.h"
#include "hello.h"
#include "lsa.h"
#include "routing.h"
#include "view.h"
#include "dijkstra.h"

// Déclarations des variables globales
neighbor_t neighbors[MAX_NEIGHBORS];
int neighbor_count = 0;
pthread_mutex_t neighbor_mutex = PTHREAD_MUTEX_INITIALIZER;

interface_t interfaces[MAX_INTERFACES];
int interface_count = 0;

route_t routing_table[MAX_ROUTES];
int route_count = 0;
pthread_mutex_t routing_mutex = PTHREAD_MUTEX_INITIALIZER;

lsa_t topology_db[MAX_NEIGHBORS];
int topology_db_size = 0;
pthread_mutex_t topology_mutex = PTHREAD_MUTEX_INITIALIZER;

dijkstra_node_t nodes[MAX_NEIGHBORS];
int node_count = 0;

volatile int running = 1;
int broadcast_sock = -1;
int listen_sock = -1;

int main(int argc, char *argv[])
{
    pthread_t listen_tid, hello_tid, lsa_tid;
    char input[BUFFER_SIZE];
    char hostname[256];

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    printf("=== Routeur Communication System ===\n");
    printf("🖥️  Routeur: %s\n", hostname);
    printf("🌐 Réseau broadcast: %s:%d\n", BROADCAST_IP, BROADCAST_PORT);
    printf("=====================================\n\n");

    // Découverte des interfaces réseau
    printf("🔍 Découverte des interfaces réseau...\n");
    if (discover_interfaces() <= 0)
    {
        printf("❌ Aucune interface réseau découverte\n");
        return 1;
    }

    ensure_local_routes();

    // Création du socket de broadcast
    broadcast_sock = create_broadcast_socket();
    if (broadcast_sock < 0)
    {
        return 1;
    }

    // Lancement des threads
    printf("🚀 Démarrage des services...\n");

    if (pthread_create(&listen_tid, NULL, listen_thread, NULL) != 0 ||
        pthread_create(&hello_tid, NULL, hello_thread, NULL) != 0 ||
        pthread_create(&lsa_tid, NULL, lsa_thread, NULL) != 0)
    {
        perror("Erreur création d’un des threads");
        close(broadcast_sock);
        return 1;
    }

    sleep(2); // Temps pour initialisation des threads
    initialize_own_lsa();

    printf("✅ Tous les services sont actifs\n\n");
    printf("💬 Commandes disponibles:\n");
    printf("  - Tapez votre message pour l'envoyer\n");
    printf("  - 'neighbors' : Afficher les voisins\n");
    printf("  - 'routes' : Afficher la table de routage\n");
    printf("  - 'topology' : Afficher la topologie\n");
    printf("  - 'debug' : Voir la base topologique\n");
    printf("  - 'status' : État du système\n");
    printf("  - 'quit' ou 'exit' : Quitter\n\n");

    while (running)
    {
        printf("💬 Commande: ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0;

        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0)
        {
            system("ip route flush table 100");
            break;
        }
        else if (strcmp(input, "neighbors") == 0)
        {
            show_neighbors();
        }
        else if (strcmp(input, "routes") == 0)
        {
            printf("🔄 Calcul des routes...\n");
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += 3;

            if (pthread_mutex_timedlock(&routing_mutex, &timeout) == 0)
            {
                show_routing_table();
                pthread_mutex_unlock(&routing_mutex);
            }
            else
            {
                printf("❌ Timeout - impossible d'accéder aux routes\n");
            }
        }
        else if (strcmp(input, "topology") == 0)
        {
            printf("🔄 Lecture de la topologie...\n");
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += 3;

            if (pthread_mutex_timedlock(&topology_mutex, &timeout) == 0)
            {
                show_topology();
                pthread_mutex_unlock(&topology_mutex);
            }
            else
            {
                printf("❌ Timeout - impossible d'accéder à la topologie\n");
            }
        }
        else if (strcmp(input, "debug") == 0)
        {
            debug_topology_db();
        }
        else if (strcmp(input, "status") == 0)
        {
            check_system_status();
        }
        else if (strlen(input) > 0)
        {
            send_message(input);
        }
    }

    // Fin du programme proprement
    running = 0;
    if (broadcast_sock >= 0) close(broadcast_sock);
    if (listen_sock >= 0) close(listen_sock);

    struct timespec timeout_spec = {2, 0};
    printf("🛑 Arrêt des services...\n");

    if (pthread_timedjoin_np(listen_tid, NULL, &timeout_spec) != 0)
    {
        printf("Timeout - arrêt forcé du thread d'écoute\n");
        pthread_cancel(listen_tid);
    }

    if (pthread_timedjoin_np(hello_tid, NULL, &timeout_spec) != 0)
    {
        printf("Timeout - arrêt forcé du thread Hello\n");
        pthread_cancel(hello_tid);
    }

    if (pthread_timedjoin_np(lsa_tid, NULL, &timeout_spec) != 0)
    {
        printf("Timeout - arrêt forcé du thread LSA\n");
        pthread_cancel(lsa_tid);
    }

    printf("👋 Programme terminé.\n");
    return 0;
}
