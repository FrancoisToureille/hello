#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/select.h>
#include <limits.h>

#define BROADCAST_PORT 8080
#define BUFFER_SIZE 1024
#define BROADCAST_IP "10.1.0.255"
// Ajoutez ces définitions après les #define existants
#define MAX_NEIGHBORS 10
#define MAX_INTERFACES 5
#define HELLO_INTERVAL 10
#define NEIGHBOR_TIMEOUT 30
#define MAX_ROUTES 50

// Structure pour un voisin direct
typedef struct
{
    char router_id[32];
    char ip_address[16];
    char interface[16];
    int metric;
    time_t last_hello;
    int bandwidth_mbps;
    int link_state; // 1=UP, 0=DOWN
} neighbor_t;

// Structure pour une interface réseau
typedef struct
{
    char name[16];
    char ip_address[16];
    char broadcast_ip[16];
    int is_active;
} interface_t;

// Table des voisins globale
neighbor_t neighbors[MAX_NEIGHBORS];
int neighbor_count = 0;
interface_t interfaces[MAX_INTERFACES];
int interface_count = 0;
pthread_mutex_t neighbor_mutex = PTHREAD_MUTEX_INITIALIZER;

// Structure pour une route
typedef struct
{
    char destination[16];
    char next_hop[16];
    char interface[16];
    int metric;
    int hop_count;
    int bandwidth;
} route_t;

// Table de routage
route_t routing_table[MAX_ROUTES];
int route_count = 0;
pthread_mutex_t routing_mutex = PTHREAD_MUTEX_INITIALIZER;

// Structure pour un Link State Advertisement
typedef struct
{
    char router_id[32];
    int sequence_number;
    time_t timestamp;
    int num_links;
    neighbor_t links[MAX_NEIGHBORS];
} lsa_t;

// Base de données topologique
lsa_t topology_db[MAX_NEIGHBORS];
int topology_db_size = 0;
pthread_mutex_t topology_mutex = PTHREAD_MUTEX_INITIALIZER;

// Structures pour l'algorithme de Dijkstra
typedef struct
{
    char router_id[32];
    char ip_address[16];
    int distance;
    char next_hop[16];
    char interface[16];
    int visited;
    int bandwidth;
} dijkstra_node_t;

dijkstra_node_t nodes[MAX_NEIGHBORS];
int node_count = 0;

volatile int running = 1;
int broadcast_sock;
int listen_sock; // Socket d'écoute global pour pouvoir le fermer

// Function declarations
void update_kernel_routing_table(void);
void process_hello_message(const char *message, const char *sender_ip);
void process_lsa_message(const char *message, const char *sender_ip);
void broadcast_lsa(const char *lsa_message);
void show_topology(void);

void signal_handler(int sig)
{
    running = 0;
    printf("\nArrêt du programme...\n");

    // Fermer les sockets pour débloquer les threads
    if (broadcast_sock >= 0)
    {
        close(broadcast_sock);
    }
    if (listen_sock >= 0)
    {
        close(listen_sock);
    }
}

void print_usage(char *program_name)
{
    printf("Usage: %s\n", program_name);
    printf("Le programme démarre automatiquement en mode écoute.\n");
    printf("Tapez vos messages et appuyez sur Entrée pour les envoyer.\n");
    printf("Tapez 'quit' ou 'exit' pour quitter.\n");
}

int create_broadcast_socket()
{
    int sock;
    int broadcast_enable = 1;
    int reuse = 1;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("Erreur création socket");
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
                   &broadcast_enable, sizeof(broadcast_enable)) < 0)
    {
        perror("Erreur setsockopt SO_BROADCAST");
        close(sock);
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) < 0)
    {
        perror("Erreur setsockopt SO_REUSEADDR");
        close(sock);
        return -1;
    }

    return sock;
}
void *listen_thread(void *arg)
{
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    socklen_t client_len = sizeof(client_addr);
    ssize_t bytes_received;
    char hostname[256];
    fd_set readfds;
    struct timeval timeout;

    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    listen_sock = create_broadcast_socket();
    if (listen_sock < 0)
    {
        pthread_exit(NULL);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(BROADCAST_PORT);

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Erreur bind");
        close(listen_sock);
        pthread_exit(NULL);
    }

    printf("🔊 Écoute active sur le port %d\n", BROADCAST_PORT);

    while (running)
    {
        FD_ZERO(&readfds);
        FD_SET(listen_sock, &readfds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int select_result = select(listen_sock + 1, &readfds, NULL, NULL, &timeout);

        if (select_result < 0)
        {
            if (errno == EINTR || !running)
            {
                break;
            }
            perror("Erreur select");
            break;
        }
        else if (select_result == 0)
        {
            continue;
        }

        if (FD_ISSET(listen_sock, &readfds))
        {
            bytes_received = recvfrom(listen_sock, buffer, BUFFER_SIZE - 1, 0,
                                      (struct sockaddr *)&client_addr, &client_len);

            if (bytes_received > 0)
            {
                buffer[bytes_received] = '\0';

                // Déterminer le type de message
                if (strncmp(buffer, "HELLO|", 6) == 0)
                {
                    // Traiter message Hello
                    process_hello_message(buffer, inet_ntoa(client_addr.sin_addr));
                }
                else if (strncmp(buffer, "LSA|", 4) == 0)
                {
                    // Traiter message LSA
                    process_lsa_message(buffer, inet_ntoa(client_addr.sin_addr));
                }
                else
                {
                    // Message utilisateur normal
                    if (strstr(buffer, hostname) != buffer + 1)
                    {
                        time_t now = time(NULL);
                        char *time_str = ctime(&now);
                        time_str[strlen(time_str) - 1] = '\0';

                        printf("\n📨 [%s] Reçu de %s: %s\n",
                               time_str, inet_ntoa(client_addr.sin_addr), buffer);
                        printf("💬 Commande: ");
                        fflush(stdout);
                    }
                }
            }
        }
    }

    close(listen_sock);
    listen_sock = -1;
    pthread_exit(NULL);
}

int send_message(const char *message)
{
    struct sockaddr_in broadcast_addr;
    char hostname[256];
    char full_message[BUFFER_SIZE];

    if (!running)
        return -1; // Ne pas envoyer si arrêt en cours

    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    snprintf(full_message, sizeof(full_message), "[%s] %s", hostname, message);

    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(BROADCAST_PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr(BROADCAST_IP);

    if (sendto(broadcast_sock, full_message, strlen(full_message), 0,
               (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0)
    {
        if (running)
        { // Ne pas afficher l'erreur si arrêt en cours
            perror("Erreur sendto");
        }
        return -1;
    }

    printf("✅ Message envoyé: %s\n", message);
    return 0;
}
// Function to discover network interfaces
int discover_interfaces()
{
    FILE *fp;
    char line[256];
    char interface_name[16] = {0}, ip[16] = {0}, broadcast[16] = {0};
    interface_count = 0;

    fp = popen("ip -o -4 addr show | awk '{print $2,$4}'", "r");
    if (fp == NULL) {
        perror("Erreur popen ip a");
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL && interface_count < MAX_INTERFACES) {
        // Format attendu : eth0 192.168.1.1/24
        if (sscanf(line, "%15s %15s", interface_name, ip) == 2) {
            // Retirer le /xx du préfixe
            char *slash = strchr(ip, '/');
            if (slash) *slash = '\0';

            // Calculer l'adresse de broadcast (optionnel, à adapter si besoin)
            char *dot1 = strchr(ip, '.');
            char *dot2 = dot1 ? strchr(dot1 + 1, '.') : NULL;
            char *dot3 = dot2 ? strchr(dot2 + 1, '.') : NULL;
            if (dot1 && dot2 && dot3) {
                int a = atoi(ip);
                int b = atoi(dot1 + 1);
                int c = atoi(dot2 + 1);
                snprintf(broadcast, sizeof(broadcast), "%d.%d.%d.255", a, b, c);
            } else {
                strcpy(broadcast, "255.255.255.255");
            }

            strcpy(interfaces[interface_count].name, interface_name);
            strcpy(interfaces[interface_count].ip_address, ip);
            strcpy(interfaces[interface_count].broadcast_ip, broadcast);
            interfaces[interface_count].is_active = 1;
            interface_count++;

            printf("🔍 Interface découverte: %s (%s) -> broadcast %s\n",
                   interface_name, ip, broadcast);
        }
    }
    pclose(fp);

    return interface_count;
}
void ensure_local_routes()
{
    for (int i = 0; i < interface_count; i++) {
        // Construire le préfixe réseau (ex : 192.168.1.0/24)
        char prefix[32];
        strcpy(prefix, interfaces[i].ip_address);
        char *last_dot = strrchr(prefix, '.');
        if (last_dot) strcpy(last_dot + 1, "0/24");

        // Vérifier si la route existe déjà
        char check_cmd[128];
        snprintf(check_cmd, sizeof(check_cmd),
            "ip route show | grep -q '^%s '", prefix);
        int exists = system(check_cmd);

        if (exists != 0) {
            // Ajouter la route
            char add_cmd[256];
            snprintf(add_cmd, sizeof(add_cmd),
                "ip route add %s dev %s", prefix, interfaces[i].name);
            printf("🛣️  Ajout de la route locale : %s\n", add_cmd);
            system(add_cmd);
        }
    }
}

// Function to process hello messages
void process_hello_message(const char *message, const char *sender_ip)
{
    printf("🔍 Message reçu: %s (de %s)\n", message, sender_ip);

    if (strncmp(message, "HELLO|", 6) == 0)
    {
        char *msg_ptr = (char *)message + 6; // Skip "HELLO|"
        char *router_id_end = strchr(msg_ptr, '|');

        if (router_id_end)
        {
            // Extract router_id
            size_t router_id_len = router_id_end - msg_ptr;
            char router_id[32];
            if (router_id_len < sizeof(router_id))
            {
                strncpy(router_id, msg_ptr, router_id_len);
                router_id[router_id_len] = '\0';

                // Vérifier si le HELLO vient de nous-même
                char hostname[256];
                if (gethostname(hostname, sizeof(hostname)) != 0)
                {
                    strcpy(hostname, "Unknown");
                }
                if (strcmp(router_id, hostname) == 0)
                {
                    // On ignore notre propre HELLO
                    return;
                }

                // Extract IP address
                char *ip_start = router_id_end + 1;
                char *ip_end = strchr(ip_start, '|');

                if (ip_end)
                {
                    size_t ip_len = ip_end - ip_start;
                    char router_ip[16];
                    if (ip_len < sizeof(router_ip))
                    {
                        strncpy(router_ip, ip_start, ip_len);
                        router_ip[ip_len] = '\0';

                        pthread_mutex_lock(&neighbor_mutex);

                        // Check if neighbor already exists
                        int found = -1;
                        for (int i = 0; i < neighbor_count; i++)
                        {
                            if (strcmp(neighbors[i].router_id, router_id) == 0)
                            {
                                found = i;
                                break;
                            }
                        }

                        if (found >= 0)
                        {
                            neighbors[found].last_hello = time(NULL);
                            neighbors[found].link_state = 1;
                            printf("🔄 Mise à jour voisin: %s\n", router_id);
                        }
                        else if (neighbor_count < MAX_NEIGHBORS)
                        {
                            strcpy(neighbors[neighbor_count].router_id, router_id);
                            strcpy(neighbors[neighbor_count].ip_address, router_ip);
                            neighbors[neighbor_count].metric = 1;
                            neighbors[neighbor_count].last_hello = time(NULL);
                            neighbors[neighbor_count].bandwidth_mbps = 100;
                            neighbors[neighbor_count].link_state = 1;

                            for (int j = 0; j < interface_count; j++)
                            {
                                if (strncmp(router_ip, interfaces[j].ip_address, strlen(interfaces[j].ip_address) - 2) == 0)
                                {
                                    strcpy(neighbors[neighbor_count].interface, interfaces[j].name);
                                    break;
                                }
                            }

                            printf("🤝 Nouveau voisin découvert: %s (%s)\n", router_id, router_ip);
                            neighbor_count++;
                        }

                        pthread_mutex_unlock(&neighbor_mutex);
                    }
                }
            }
        }
    }
}

// Fonction pour nettoyer les voisins expirés
void cleanup_expired_neighbors()
{
    pthread_mutex_lock(&neighbor_mutex);

    time_t now = time(NULL);
    for (int i = 0; i < neighbor_count; i++)
    {
        if (now - neighbors[i].last_hello > NEIGHBOR_TIMEOUT)
        {
            printf("❌ Voisin expiré: %s\n", neighbors[i].router_id);
            neighbors[i].link_state = 0;
        }
    }

    pthread_mutex_unlock(&neighbor_mutex);
}

// Thread pour échanger les LSA
void *lsa_thread(void *arg)
{
    char hostname[256];
    char lsa_message[BUFFER_SIZE];

    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    while (running)
    {
        // Créer et envoyer notre LSA
        pthread_mutex_lock(&neighbor_mutex);

        snprintf(lsa_message, sizeof(lsa_message), "LSA|%s|%d|%d",
                 hostname, (int)time(NULL), neighbor_count);

        // Ajouter les informations de chaque voisin
        for (int i = 0; i < neighbor_count; i++)
        {
            if (neighbors[i].link_state == 1)
            {
                char link_info[128];
                snprintf(link_info, sizeof(link_info), "|%s,%s,%d,%d",
                         neighbors[i].router_id, neighbors[i].ip_address,
                         neighbors[i].metric, neighbors[i].bandwidth_mbps);
                strcat(lsa_message, link_info);
            }
        }

        pthread_mutex_unlock(&neighbor_mutex);

        // Envoyer LSA sur toutes les interfaces
        broadcast_lsa(lsa_message);

        sleep(30); // Envoyer LSA toutes les 30 secondes
    }

    pthread_exit(NULL);
}

// Initialisation des mutex
void lock_all_mutexes()
{
    pthread_mutex_lock(&neighbor_mutex);
    printf("🔧 DEBUG: neighbor_mutex verrouillé\n");
    pthread_mutex_lock(&topology_mutex);
    printf("🔧 DEBUG: topology_mutex verrouillé\n");
    pthread_mutex_lock(&routing_mutex);
    printf("🔧 DEBUG: routing_mutex verrouillé\n");
}

void unlock_all_mutexes()
{
    printf("🔧 DEBUG: Fin calcul des chemins - déverrouillage\n");
    pthread_mutex_unlock(&routing_mutex);
    pthread_mutex_unlock(&topology_mutex);
    pthread_mutex_unlock(&neighbor_mutex);
}

// Initialiser les nœuds avec la topologie et les voisins
int initialize_nodes(dijkstra_node_t *nodes)
{
    int node_count = 0;

    for (int i = 0; i < topology_db_size; i++)
    {
        strcpy(nodes[node_count].router_id, topology_db[i].router_id);
        if (topology_db[i].num_links > 0)
        {
            strcpy(nodes[node_count].ip_address, topology_db[i].links[0].ip_address);
        }
        else
        {
            nodes[node_count].ip_address[0] = '\0';
        }
        nodes[node_count].distance = INT_MAX;
        nodes[node_count].next_hop[0] = '\0';
        nodes[node_count].interface[0] = '\0';
        nodes[node_count].visited = 0;
        nodes[node_count].bandwidth = 0;
        node_count++;
    }

    for (int i = 0; i < neighbor_count; i++)
    {
        if (neighbors[i].link_state == 1)
        {
            int found = 0;
            for (int j = 0; j < node_count; j++)
            {
                if (strcmp(nodes[j].router_id, neighbors[i].router_id) == 0)
                {
                    found = 1;
                    break;
                }
            }
            if (!found && node_count < MAX_NEIGHBORS)
            {
                strcpy(nodes[node_count].router_id, neighbors[i].router_id);
                strcpy(nodes[node_count].ip_address, neighbors[i].ip_address);
                nodes[node_count].distance = INT_MAX;
                nodes[node_count].next_hop[0] = '\0';
                nodes[node_count].interface[0] = '\0';
                nodes[node_count].visited = 0;
                nodes[node_count].bandwidth = 0;
                node_count++;
            }
        }
    }

    return node_count;
}

int find_source_index(dijkstra_node_t *nodes, int node_count, const char *hostname)
{
    for (int i = 0; i < node_count; i++)
    {
        if (strcmp(nodes[i].router_id, hostname) == 0)
        {
            return i;
        }
    }
    return -1;
}

void initialize_direct_neighbors(dijkstra_node_t *nodes, int node_count)
{
    for (int i = 0; i < neighbor_count; i++)
    {
        if (neighbors[i].link_state == 1)
        {
            for (int j = 0; j < node_count; j++)
            {
                if (strcmp(nodes[j].router_id, neighbors[i].router_id) == 0)
                {
                    int metric = neighbors[i].metric + (1000 / neighbors[i].bandwidth_mbps);
                    nodes[j].distance = metric;
                    strcpy(nodes[j].next_hop, neighbors[i].ip_address);
                    strcpy(nodes[j].interface, neighbors[i].interface);
                    nodes[j].bandwidth = neighbors[i].bandwidth_mbps;
                    break;
                }
            }
        }
    }
}

void run_dijkstra(dijkstra_node_t *nodes, int node_count)
{
    for (int count = 0; count < node_count - 1; count++)
    {
        int min_distance = INT_MAX;
        int min_index = -1;

        for (int i = 0; i < node_count; i++)
        {
            if (!nodes[i].visited && nodes[i].distance < min_distance)
            {
                min_distance = nodes[i].distance;
                min_index = i;
            }
        }

        if (min_index == -1)
            break;

        nodes[min_index].visited = 1;

        lsa_t *current_lsa = NULL;
        for (int i = 0; i < topology_db_size; i++)
        {
            if (strcmp(topology_db[i].router_id, nodes[min_index].router_id) == 0)
            {
                current_lsa = &topology_db[i];
                break;
            }
        }

        if (!current_lsa)
            continue;

        for (int i = 0; i < current_lsa->num_links; i++)
        {
            int neighbor_index = -1;
            for (int j = 0; j < node_count; j++)
            {
                if (strcmp(nodes[j].router_id, current_lsa->links[i].router_id) == 0)
                {
                    neighbor_index = j; 
                    break;
                }
            }

            if (neighbor_index >= 0 && !nodes[neighbor_index].visited)
            {
                int link_cost = current_lsa->links[i].metric + (1000 / current_lsa->links[i].bandwidth_mbps);
                int new_distance = nodes[min_index].distance + link_cost;

                if (new_distance < nodes[neighbor_index].distance)
                {
                    nodes[neighbor_index].distance = new_distance;

                    if (nodes[min_index].distance == 0)
                    {
                        strcpy(nodes[neighbor_index].next_hop, current_lsa->links[i].ip_address);
                        strcpy(nodes[neighbor_index].interface, current_lsa->links[i].interface);
                        nodes[neighbor_index].bandwidth = current_lsa->links[i].bandwidth_mbps;
                    }
                    else
                    {
                        strcpy(nodes[neighbor_index].next_hop, nodes[min_index].next_hop);
                        strcpy(nodes[neighbor_index].interface, nodes[min_index].interface);
                        nodes[neighbor_index].bandwidth = nodes[min_index].bandwidth;
                    }
                }
            }
        }
    }
}

void build_routing_table(dijkstra_node_t *nodes, int node_count, int source_index) {
    route_count = 0;

    for (int i = 0; i < node_count; i++) {
        if (i == source_index) continue;

        for (int j = 0; j < topology_db_size; j++) {
            if (strcmp(topology_db[j].router_id, nodes[i].router_id) != 0) continue;

            for (int k = 0; k < topology_db[j].num_links; k++) {
                const char *dest_ip = topology_db[j].links[k].ip_address;

                // Calcule le préfixe réseau (ex: 192.168.1.0/24)
                char prefix[32];
                strcpy(prefix, dest_ip);
                char *last_dot = strrchr(prefix, '.');
                if (last_dot) strcpy(last_dot + 1, "0/24");

                // Vérifie qu'on n'a pas déjà ajouté cette destination (évite les doublons)
                int already = 0;
                for (int r = 0; r < route_count; r++) {
                    if (strcmp(routing_table[r].destination, prefix) == 0) {
                        already = 1;
                        break;
                    }
                }
                if (already) continue;

                // Vérifie que ce n'est pas un de nos propres réseaux locaux
                int is_own_network = 0;
                for (int m = 0; m < interface_count; m++) {
                    char local_prefix[32];
                    strcpy(local_prefix, interfaces[m].ip_address);
                    char *ldot = strrchr(local_prefix, '.');
                    if (ldot) strcpy(ldot + 1, "0/24");
                    if (strcmp(prefix, local_prefix) == 0) {
                        is_own_network = 1;
                        break;
                    }
                }
                if (is_own_network) continue;

                // Ajoute la route
                if (route_count < MAX_ROUTES) {
                    strcpy(routing_table[route_count].destination, prefix);
                    strcpy(routing_table[route_count].next_hop, nodes[i].next_hop);
                    strcpy(routing_table[route_count].interface, nodes[i].interface);
                    routing_table[route_count].metric = nodes[i].distance + topology_db[j].links[k].metric;
                    routing_table[route_count].hop_count = (routing_table[route_count].metric + 999) / 1000;
                    routing_table[route_count].bandwidth = topology_db[j].links[k].bandwidth_mbps;
                    route_count++;
                }
            }
        }
    }
void build_routing_table(dijkstra_node_t *nodes, int node_count, int source_index) {
    route_count = 0;

    for (int i = 0; i < node_count; i++) {
        if (i == source_index) continue;

        for (int j = 0; j < topology_db_size; j++) {
            if (strcmp(topology_db[j].router_id, nodes[i].router_id) != 0) continue;

            for (int k = 0; k < topology_db[j].num_links; k++) {
                const char *dest_ip = topology_db[j].links[k].ip_address;

                // Calcule le préfixe réseau (ex: 192.168.1.0/24)
                char prefix[32];
                strcpy(prefix, dest_ip);
                char *last_dot = strrchr(prefix, '.');
                if (last_dot) strcpy(last_dot + 1, "0/24");

                // Vérifie qu'on n'a pas déjà ajouté cette destination (évite les doublons)
                int already = 0;
                for (int r = 0; r < route_count; r++) {
                    if (strcmp(routing_table[r].destination, prefix) == 0) {
                        already = 1;
                        break;
                    }
                }
                if (already) continue;

                // Vérifie que ce n'est pas un de nos propres réseaux locaux
                int is_own_network = 0;
                for (int m = 0; m < interface_count; m++) {
                    char local_prefix[32];
                    strcpy(local_prefix, interfaces[m].ip_address);
                    char *ldot = strrchr(local_prefix, '.');
                    if (ldot) strcpy(ldot + 1, "0/24");
                    if (strcmp(prefix, local_prefix) == 0) {
                        is_own_network = 1;
                        break;
                    }
                }
                if (is_own_network) continue;

                // Ajoute la route
                if (route_count < MAX_ROUTES) {
                    strcpy(routing_table[route_count].destination, prefix);
                    strcpy(routing_table[route_count].next_hop, nodes[i].next_hop);
                    strcpy(routing_table[route_count].interface, nodes[i].interface);
                    routing_table[route_count].metric = nodes[i].distance + topology_db[j].links[k].metric;
                    routing_table[route_count].hop_count = (routing_table[route_count].metric + 999) / 1000;
                    routing_table[route_count].bandwidth = topology_db[j].links[k].bandwidth_mbps;
                    route_count++;
                }
            }
        }
    }
}


// Fonction principale refactorisée
void calculate_shortest_paths()
{
    printf("🔧 DEBUG: Début calcul des chemins\n");
    lock_all_mutexes();

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    route_count = 0;

    dijkstra_node_t nodes[MAX_NEIGHBORS];
    int node_count = initialize_nodes(nodes);

    int source_index = find_source_index(nodes, node_count, hostname);
    if (source_index < 0)
    {
        unlock_all_mutexes();
        return;
    }

    nodes[source_index].distance = 0;
    initialize_direct_neighbors(nodes, node_count);
    run_dijkstra(nodes, node_count);
    build_routing_table(nodes, node_count, source_index);

    unlock_all_mutexes();

    update_kernel_routing_table();
    printf("🗺️  Table de routage mise à jour (%d routes calculées avec Dijkstra)\n", route_count);
}

// Fonction pour créer notre propre LSA dans la base de données
void initialize_own_lsa()
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    pthread_mutex_lock(&topology_mutex);

    // Vérifier si notre LSA existe déjà
    int our_lsa_index = -1;
    for (int i = 0; i < topology_db_size; i++)
    {
        if (strcmp(topology_db[i].router_id, hostname) == 0)
        {
            our_lsa_index = i;
            break;
        }
    }

    // Créer notre LSA si elle n'existe pas
    if (our_lsa_index < 0 && topology_db_size < MAX_NEIGHBORS)
    {
        our_lsa_index = topology_db_size;
        topology_db_size++;
    }

    if (our_lsa_index >= 0)
    {
        strcpy(topology_db[our_lsa_index].router_id, hostname);
        topology_db[our_lsa_index].sequence_number = (int)time(NULL);
        topology_db[our_lsa_index].timestamp = time(NULL);
        topology_db[our_lsa_index].num_links = 0;

                // Ajouter nos voisins directs
        pthread_mutex_lock(&neighbor_mutex);
        for (int i = 0; i < neighbor_count && topology_db[our_lsa_index].num_links < MAX_NEIGHBORS; i++)
        {
            if (neighbors[i].link_state == 1)
            {
                int link_idx = topology_db[our_lsa_index].num_links;
                topology_db[our_lsa_index].links[link_idx] = neighbors[i];
                topology_db[our_lsa_index].num_links++;
            }
        }
        pthread_mutex_unlock(&neighbor_mutex);

        // ✅ Ajouter nos propres interfaces comme liens locaux
        for (int i = 0; i < interface_count && topology_db[our_lsa_index].num_links < MAX_NEIGHBORS; i++)
        {
            neighbor_t local_link;
            memset(&local_link, 0, sizeof(local_link));
            strncpy(local_link.router_id, hostname, sizeof(local_link.router_id) - 1);
            strncpy(local_link.ip_address, interfaces[i].ip_address, sizeof(local_link.ip_address) - 1);
            strncpy(local_link.interface, interfaces[i].name, sizeof(local_link.interface) - 1);
            local_link.metric = 0;
            local_link.bandwidth_mbps = 1000;
            local_link.link_state = 1;

            topology_db[our_lsa_index].links[topology_db[our_lsa_index].num_links++] = local_link;
        }

    }

    pthread_mutex_unlock(&topology_mutex);
}

// Fonction pour afficher la table des voisins
void show_neighbors()
{
    pthread_mutex_lock(&neighbor_mutex);

    printf("\n=== Table des voisins ===\n");
    printf("%-15s %-15s %-8s %-10s %-8s\n", "Routeur", "IP", "Métrique", "Bande Pass.", "État");
    printf("--------------------------------------------------------\n");

    for (int i = 0; i < neighbor_count; i++)
    {
        printf("%-15s %-15s %-8d %-10d %-8s\n",
               neighbors[i].router_id,
               neighbors[i].ip_address,
               neighbors[i].metric,
               neighbors[i].bandwidth_mbps,
               neighbors[i].link_state ? "UP" : "DOWN");
    }

    pthread_mutex_unlock(&neighbor_mutex);
}

// Fonction pour afficher la table de routage
void show_routing_table()
{

    printf("\n=== Table de routage ===\n");
    printf("%-15s %-15s %-12s %-8s %-5s\n", "Destination", "Next Hop", "Interface", "Métrique", "Sauts");
    printf("---------------------------------------------------------------\n");

    for (int i = 0; i < route_count; i++)
    {
        printf("%-15s %-15s %-12s %-8d %-5d\n",
               routing_table[i].destination,
               routing_table[i].next_hop,
               routing_table[i].interface,
               routing_table[i].metric,
               routing_table[i].hop_count);
    }
}

// Thread pour envoyer des messages Hello périodiques
void *hello_thread(void *arg)
{
    char hostname[256];
    char hello_message[BUFFER_SIZE];

    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    while (running)
    {
        printf("📡 Envoi des messages Hello...\n");

        for (int i = 0; i < interface_count; i++)
        {
            if (interfaces[i].is_active)
            {
                snprintf(hello_message, sizeof(hello_message),
                         "HELLO|%s|%s|%d", hostname, interfaces[i].ip_address, (int)time(NULL));

                printf("  -> Interface %s (%s) vers %s\n",
                       interfaces[i].name, interfaces[i].ip_address, interfaces[i].broadcast_ip);

                int hello_sock = create_broadcast_socket();
                if (hello_sock >= 0)
                {
                    struct sockaddr_in broadcast_addr;
                    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
                    broadcast_addr.sin_family = AF_INET;
                    broadcast_addr.sin_port = htons(BROADCAST_PORT);
                    broadcast_addr.sin_addr.s_addr = inet_addr(interfaces[i].broadcast_ip);

                    if (sendto(hello_sock, hello_message, strlen(hello_message), 0,
                               (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0)
                    {
                        perror("Erreur envoi Hello");
                    }
                    else
                    {
                        printf("  ✅ Hello envoyé sur %s\n", interfaces[i].broadcast_ip);
                    }

                    close(hello_sock);
                }
                else
                {
                    printf("  ❌ Erreur création socket Hello\n");
                }
            }
        }

        cleanup_expired_neighbors();

        // Mettre à jour notre LSA avec les nouveaux voisins
        initialize_own_lsa();

        sleep(HELLO_INTERVAL);
    }

    pthread_exit(NULL);
}

void flood_lsa(const char *lsa_message, const char *sender_ip)
{
    int flood_sock = create_broadcast_socket();
    if (flood_sock < 0)
        return;

    for (int i = 0; i < interface_count; i++)
    {
        if (interfaces[i].is_active)
        {
            struct sockaddr_in dest;
            memset(&dest, 0, sizeof(dest));
            dest.sin_family = AF_INET;
            dest.sin_port = htons(BROADCAST_PORT);
            dest.sin_addr.s_addr = inet_addr(interfaces[i].broadcast_ip);

            sendto(flood_sock, lsa_message, strlen(lsa_message), 0,
                   (struct sockaddr *)&dest, sizeof(dest));

            printf("📡 Flood LSA sur %s (%s)\n", interfaces[i].name, interfaces[i].broadcast_ip);
        }
    }

    close(flood_sock);
}
// Function to process LSA messages
void process_lsa_message(const char *message, const char *sender_ip)
{
    // Format: LSA|router_id|timestamp|num_links|router_id,ip,metric,bandwidth|...
    char msg_copy[BUFFER_SIZE];
    strncpy(msg_copy, message, sizeof(msg_copy));
    msg_copy[sizeof(msg_copy) - 1] = '\0';

    char *saveptr;
    char *token = strtok_r(msg_copy, "|", &saveptr); // "LSA"
    if (!token || strcmp(token, "LSA") != 0)
        return;

    char *lsa_router_id = strtok_r(NULL, "|", &saveptr);
    char *lsa_timestamp_str = strtok_r(NULL, "|", &saveptr);
    char *lsa_num_links_str = strtok_r(NULL, "|", &saveptr);

    if (!lsa_router_id || !lsa_timestamp_str || !lsa_num_links_str)
        return;

    // Ignorer nos propres LSA
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
        strcpy(hostname, "Unknown");

    if (strcmp(lsa_router_id, hostname) == 0)
    {
        printf("🔇 Ignoré LSA de moi-même (%s)\n", hostname);
        return;
    }

    int lsa_num_links = atoi(lsa_num_links_str);

    lsa_t new_lsa;
    memset(&new_lsa, 0, sizeof(new_lsa));
    strncpy(new_lsa.router_id, lsa_router_id, sizeof(new_lsa.router_id) - 1);
    new_lsa.timestamp = atoi(lsa_timestamp_str);
    new_lsa.num_links = 0;

    for (int i = 0; i < lsa_num_links; i++)
    {
        char *link_str = strtok_r(NULL, "|", &saveptr);
        if (!link_str)
            break;
        char *field_ptr;
        char *router_id = strtok_r(link_str, ",", &field_ptr);
        char *ip = strtok_r(NULL, ",", &field_ptr);
        char *metric_str = strtok_r(NULL, ",", &field_ptr);
        char *bw_str = strtok_r(NULL, ",", &field_ptr);
        if (!router_id || !ip || !metric_str || !bw_str)
            continue;
        neighbor_t link;
        memset(&link, 0, sizeof(link));
        strncpy(link.router_id, router_id, sizeof(link.router_id) - 1);
        strncpy(link.ip_address, ip, sizeof(link.ip_address) - 1);
        link.metric = atoi(metric_str);
        link.bandwidth_mbps = atoi(bw_str);
        link.link_state = 1;
        new_lsa.links[new_lsa.num_links++] = link;
    }
    int updated = 0;
    pthread_mutex_lock(&topology_mutex);
    int found = -1;
    for (int i = 0; i < topology_db_size; i++)
    {
        if (strcmp(topology_db[i].router_id, new_lsa.router_id) == 0)
        {
            // Remplacer si timestamp plus récent
            if (new_lsa.timestamp > topology_db[i].timestamp)
            {
                topology_db[i] = new_lsa;
                updated = 1; // LSDB modifiée
            }
            found = 1;
            break;
        }
    }
    if (found == -1 && topology_db_size < MAX_NEIGHBORS)
    {
        topology_db[topology_db_size++] = new_lsa;
        updated = 1;
    }
    pthread_mutex_unlock(&topology_mutex);

    // NE recalculer/flooder QUE si la LSDB a changé
    if (updated)
    {
        calculate_shortest_paths();
        flood_lsa(message, sender_ip);
    }
}
void update_kernel_routing_table()
{
    // Ne supprime que les routes dont le next-hop n'est pas 0.0.0.0 (pas les locales)
    system("ip route flush table 100");

    pthread_mutex_lock(&routing_mutex);
    for (int i = 0; i < route_count; i++)
    {
        // destination est une IP
        char cmd[256];
        // N'ajoute pas de route si next_hop == 0.0.0.0 (c'est une route locale)
        if (strcmp(routing_table[i].next_hop, "0.0.0.0") == 0)
            continue;

        snprintf(cmd, sizeof(cmd),
                 "ip route replace %s via %s dev %s",
                 routing_table[i].destination,
                 routing_table[i].next_hop,
                 routing_table[i].interface);
        printf("🛣️  Ajout route OSPF : %s\n", cmd);
        int ret = system(cmd);
        if (ret != 0)
        {
            printf("⚠️  Erreur lors de l'ajout de la route: %s\n", cmd);
        }
    }
    pthread_mutex_unlock(&routing_mutex);
}


// Function to broadcast LSA messages
void broadcast_lsa(const char *lsa_message)
{
    struct sockaddr_in broadcast_addr;

    for (int i = 0; i < interface_count; i++)
    {
        if (interfaces[i].is_active)
        {
            memset(&broadcast_addr, 0, sizeof(broadcast_addr));
            broadcast_addr.sin_family = AF_INET;
            broadcast_addr.sin_port = htons(BROADCAST_PORT);
            broadcast_addr.sin_addr.s_addr = inet_addr(interfaces[i].broadcast_ip);

            sendto(broadcast_sock, lsa_message, strlen(lsa_message), 0,
                   (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        }
    }
}

// Function to display the network topology
void show_topology(void)
{

    printf("\n=== Topologie du réseau ===\n");
    printf("%-15s %-8s %-10s %-15s\n", "Routeur", "Séquence", "Liens", "Voisins");
    printf("--------------------------------------------------------\n");

    for (int i = 0; i < topology_db_size; i++)
    {
        printf("%-15s %-8d %-10d ",
               topology_db[i].router_id,
               topology_db[i].sequence_number,
               topology_db[i].num_links);

        // Print first neighbor
        int first_printed = 0;
        for (int j = 0; j < topology_db[i].num_links; j++)
        {
            // Ne pas afficher le routeur lui-même
            if (strcmp(topology_db[i].links[j].router_id, topology_db[i].router_id) != 0)
            {
                if (first_printed)
                {
                    printf(", ");
                }
                printf("%s", topology_db[i].links[j].router_id);
                first_printed = 1;
            }
        }
        printf("\n");
    }
}
void debug_topology_db()
{
    pthread_mutex_lock(&topology_mutex);
    printf("\n=== DEBUG: Base de données topologique ===\n");
    for (int i = 0; i < topology_db_size; i++)
    {
        printf("Routeur %s: %d liens\n", topology_db[i].router_id, topology_db[i].num_links);
        for (int j = 0; j < topology_db[i].num_links; j++)
        {
            printf("  -> %s (%s)\n",
                   topology_db[i].links[j].router_id,
                   topology_db[i].links[j].ip_address);
        }
    }
    pthread_mutex_unlock(&topology_mutex);
}
void check_system_status()
{
    printf("=== État du système ===\n");
    printf("Running: %s\n", running ? "OUI" : "NON");
    printf("Voisins: %d\n", neighbor_count);
    printf("Topologie DB: %d entrées\n", topology_db_size);
    printf("Routes: %d\n", route_count);
    printf("Interfaces: %d\n", interface_count);
    printf("======================\n");
}
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

    // Attendre que tous les threads se terminent
    struct timespec timeout_spec;
    timeout_spec.tv_sec = 2;
    timeout_spec.tv_nsec = 0;

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