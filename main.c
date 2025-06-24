//main.c
#include "types.h"
#include "view.h"
#include "hello.h"
#include "lsa.h"
#include "routing.h"
#include "control.h"

// déclarations globales
neighbor_t neighbors[MAX_NEIGHBORS];
interface_t interfaces[MAX_INTERFACES];
route_t routing_table[MAX_ROUTES];
lsa_t topology_db[MAX_NEIGHBORS];
dijkstra_node_t nodes[MAX_NEIGHBORS];

int neighbor_count = 0;
int interface_count = 0;
int route_count = 0;
int topology_db_size = 0;
int node_count = 0;
int broadcast_sock;
int listen_sock;
volatile int running = 1;

// mutex
pthread_mutex_t neighbor_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t routing_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t topology_mutex = PTHREAD_MUTEX_INITIALIZER;

// prototypes
void update_kernel_routing_table(void);
void process_hello_message(const char *message, const char *sender_ip);
void process_lsa_message(const char *message, const char *sender_ip);
void broadcast_lsa(const char *lsa_message);
void show_topology(void);

int main(int argc, char *argv[])
{
    pthread_t listen_tid, hello_tid, lsa_tid;
    char input[BUFFER_SIZE];
    char hostname[256];

    // Initialiser les sockets à -1
    broadcast_sock = -1;
    listen_sock = -1;

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

    // ÉTAPE 1: Découvrir les interfaces réseau EN PREMIER
    printf("🔍 Découverte des interfaces réseau...\n");
    if (discover_interfaces() <= 0)
    {
        printf("❌ Aucune interface réseau découverte\n");
        return 1;
    }

    ensure_local_routes();

    // ÉTAPE 2: Créer le socket de broadcast
    broadcast_sock = create_broadcast_socket();
    if (broadcast_sock < 0)
    {
        return 1;
    }

    // ÉTAPE 3: Démarrer TOUS les threads
    printf("🚀 Démarrage des services...\n");

    if (pthread_create(&listen_tid, NULL, listen_thread, NULL) != 0)
    {
        perror("Erreur création thread d'écoute");
        close(broadcast_sock);
        return 1;
    }

    if (pthread_create(&hello_tid, NULL, hello_thread, NULL) != 0)
    {
        perror("Erreur création thread Hello");
        close(broadcast_sock);
        return 1;
    }

    if (pthread_create(&lsa_tid, NULL, lsa_thread, NULL) != 0)
    {
        perror("Erreur création thread LSA");
        close(broadcast_sock);
        return 1;
    }

    // Attendre que tous les services se lancent
    sleep(2);

    // Initialiser notre propre LSA dans la base de données
    initialize_own_lsa();

    printf("✅ Tous les services sont actifs\n\n");
    printf("💬 Commandes disponibles:\n");
    printf("  - Tapez votre message pour l'envoyer\n");
    printf("  - 'neighbors' : Afficher les voisins\n");
    printf("  - 'routes' : Afficher la table de routage\n");
    printf("  - 'topology' : Afficher la topologie\n");
    printf("  - 'quit' ou 'exit' : Quitter\n\n");

    // ÉTAPE 4: Boucle principale avec commandes
    while (running)
    {
        printf("💬 Commande: ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            break;
        }

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
            timeout.tv_sec += 3; // Timeout de 3 secondes

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
            timeout.tv_sec += 3; // Timeout de 3 secondes

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
        else if (strlen(input) > 0)
        {
            send_message(input);
        }
    }

    // ÉTAPE 5: Nettoyage et arrêt propre
    running = 0;

    // Fermer les sockets pour débloquer les threads
    if (broadcast_sock >= 0)
    {
        close(broadcast_sock);
        broadcast_sock = -1;
    }
    if (listen_sock >= 0)
    {
        close(listen_sock);
        listen_sock = -1;
    }

    struct timespec timeout_spec;
    timeout_spec.tv_sec = 2;
    timeout_spec.tv_nsec = 0;

    printf(".........Closing services......\n");

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