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

voisin_t neighbors[MAX_NEIGHBORS];
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

noeud_dijkstra_t nodes[MAX_NEIGHBORS];
int node_count = 0;

volatile int running = 1;
int broadcast_sock = -1;
int listen_sock = -1;

int main(int argc, char *argv[])
{
    pthread_t listen_tid, hello_tid, lsa_tid;
    char input[LEN_BUFFER];
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }
    signal(SIGINT, gestion_signal);
    signal(SIGTERM, gestion_signal);

    printf("=====================================\n\n");

    if (voir_interfaces_locales() <= 0)
    {
        printf("Pas interfaces réseau \n");
        return 1;
    }

    ajouter_routes_locales();

    broadcast_sock = creer_socket_diffusion();
    if (broadcast_sock < 0)
    {
        return 1;
    }

    if (pthread_create(&listen_tid, NULL, thread_ecoute, NULL) != 0 ||
        pthread_create(&hello_tid, NULL, thread_hello, NULL) != 0 ||
        pthread_create(&lsa_tid, NULL, thread_lsa, NULL) != 0)
    {
        perror("Erreur création d’un des threads");
        close(broadcast_sock);
        return 1;
    }
    sleep(2); 
    init_lsa();

    printf("💬 Commandes disponibles:\n");
    printf("  - 'voisins'\n");
    printf("  - 'routes'\n");
    printf("  - 'exit'\n\n");

    while (running)
    {
        printf("💬 Commande: ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0;

        if (strcmp(input, "exit") == 0)
        {
            system("ip route flush table 100");
            break;
        }
        else if (strcmp(input, "voisins") == 0)
        {
            voir_voisins();
        }
        else if (strcmp(input, "routes") == 0)
        {
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += 3;

            if (pthread_mutex_timedlock(&routing_mutex, &timeout) == 0)
            {
                voir_table_routage();
                pthread_mutex_unlock(&routing_mutex);
            }
        }
        else if (strlen(input) > 0)
        {
            envoyer_message(input);
        }
    }

    running = 0;
    if (broadcast_sock >= 0) close(broadcast_sock);
    if (listen_sock >= 0) close(listen_sock);

    struct timespec timeout_spec = {2, 0};

    pthread_t threads[] = {listen_tid, hello_tid, lsa_tid};
    const char *thread_names[] = {"d'écoute", "Hello", "LSA"};
    size_t num_threads = sizeof(threads) / sizeof(threads[0]);

    for (size_t i = 0; i < num_threads; ++i)
    {
        if (pthread_timedjoin_np(threads[i], NULL, &timeout_spec) != 0)
        {
            printf("Arrêt forcé du thread %s\n", thread_names[i]);
            pthread_cancel(threads[i]);
        }
    }
    printf("END.\n");
    return 0;
}
