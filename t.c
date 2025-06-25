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
int listen_sock;

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

                if (strncmp(buffer, "HELLO|", 6) == 0)
                {
                    process_hello_message(buffer, inet_ntoa(client_addr.sin_addr));
                }
                else if (strncmp(buffer, "LSA|", 4) == 0)
                {
                    process_lsa_message(buffer, inet_ntoa(client_addr.sin_addr));
                }
                else
                {
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
    if (!running)
        return -1;

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
        strcpy(hostname, "Unknown");

    char full_message[BUFFER_SIZE];
    snprintf(full_message, sizeof(full_message), "[%s] %s", hostname, message);

    for (int i = 0; i < interface_count; i++) {
        if (!interfaces[i].is_active) continue;

        struct sockaddr_in broadcast_addr;
        memset(&broadcast_addr, 0, sizeof(broadcast_addr));
        broadcast_addr.sin_family = AF_INET;
        broadcast_addr.sin_port = htons(BROADCAST_PORT);
        broadcast_addr.sin_addr.s_addr = inet_addr(interfaces[i].broadcast_ip);

        int sock = create_broadcast_socket();
        if (sock < 0) continue;

        if (sendto(sock, full_message, strlen(full_message), 0,
                   (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0)
        {
            if (running) perror("Erreur sendto dans send_message()");
        } else {
            printf("✅ Message envoyé sur %s : %s\n", interfaces[i].broadcast_ip, message);
        }

        close(sock);
    }

    return 0;
}

// CORRECTION 1: Amélioration de la découverte d'interfaces
int discover_interfaces()
{
    FILE *fp;
    char line[256];
    interface_count = 0;

    // Utiliser une méthode plus robuste pour découvrir les interfaces
    fp = popen("ip -4 addr show | grep -E '^[0-9]+:|inet ' | grep -v '127.0.0.1'", "r");
    if (fp == NULL) {
        perror("Erreur popen ip addr");
        return -1;
    }

    char current_interface[16] = {0};
    while (fgets(line, sizeof(line), fp) != NULL && interface_count < MAX_INTERFACES) {
        // Ligne d'interface: "2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> ..."
        if (line[0] >= '0' && line[0] <= '9') {
            char *colon1 = strchr(line, ':');
            char *colon2 = colon1 ? strchr(colon1 + 1, ':') : NULL;
            if (colon1 && colon2) {
                size_t len = colon2 - colon1 - 1;
                if (len < sizeof(current_interface)) {
                    strncpy(current_interface, colon1 + 2, len); // +2 pour ignorer ": "
                    current_interface[len] = '\0';
                    
                    // Enlever les espaces en fin
                    char *end = current_interface + strlen(current_interface) - 1;
                    while (end > current_interface && *end == ' ') {
                        *end = '\0';
                        end--;
                    }
                }
            }
        }
        // Ligne IP: "    inet 192.168.1.1/24 brd 192.168.1.255 ..."
        else if (strstr(line, "inet ") && current_interface[0]) {
            char ip_with_mask[32];
            if (sscanf(line, "%*s inet %31s", ip_with_mask) == 1) {
                char *slash = strchr(ip_with_mask, '/');
                if (slash) *slash = '\0';
                
                // Calculer l'adresse de broadcast
                char broadcast[16];
                unsigned long ip_addr = inet_addr(ip_with_mask);
                unsigned long network = ip_addr & 0x00FFFFFF; // Masque /24
                unsigned long bcast = network | 0xFF000000;
                struct in_addr bcast_addr;
                bcast_addr.s_addr = bcast;
                strcpy(broadcast, inet_ntoa(bcast_addr));
                
                strcpy(interfaces[interface_count].name, current_interface);
                strcpy(interfaces[interface_count].ip_address, ip_with_mask);
                strcpy(interfaces[interface_count].broadcast_ip, broadcast);
                interfaces[interface_count].is_active = 1;
                interface_count++;
                
                printf("🔍 Interface découverte: %s (%s) -> broadcast %s\n",
                       current_interface, ip_with_mask, broadcast);
                
                current_interface[0] = '\0'; // Reset pour la prochaine interface
            }
        }
    }
    pclose(fp);

    return interface_count;
}

void ensure_local_routes()
{
    for (int i = 0; i < interface_count; i++) {
        char prefix[32];
        strcpy(prefix, interfaces[i].ip_address);
        char *last_dot = strrchr(prefix, '.');
        if (last_dot) strcpy(last_dot + 1, "0/24");

        char check_cmd[128];
        snprintf(check_cmd, sizeof(check_cmd),
            "ip route show | grep -q '^%s '", prefix);
        int exists = system(check_cmd);

        if (exists != 0) {
            char add_cmd[256];
            snprintf(add_cmd, sizeof(add_cmd),
                "ip route add %s dev %s", prefix, interfaces[i].name);
            printf("🛣️  Ajout de la route locale : %s\n", add_cmd);
            system(add_cmd);
        }
    }
}

// CORRECTION 2: Amélioration du traitement des messages Hello
void process_hello_message(const char *message, const char *sender_ip)
{
    printf("🔍 Message HELLO reçu: %s (de %s)\n", message, sender_ip);

    if (strncmp(message, "HELLO|", 6) != 0) return;

    char msg_copy[BUFFER_SIZE];
    strncpy(msg_copy, message, sizeof(msg_copy));
    msg_copy[sizeof(msg_copy) - 1] = '\0';

    char *saveptr;
    char *token = strtok_r(msg_copy, "|", &saveptr); // "HELLO"
    char *router_id = strtok_r(NULL, "|", &saveptr);
    char *router_ip = strtok_r(NULL, "|", &saveptr);
    char *timestamp_str = strtok_r(NULL, "|", &saveptr);

    if (!router_id || !router_ip) {
        printf("❌ Message HELLO malformé\n");
        return;
    }

    // Vérifier si le HELLO vient de nous-même
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strcpy(hostname, "Unknown");
    }
    if (strcmp(router_id, hostname) == 0) {
        return; // Ignorer notre propre HELLO
    }

    pthread_mutex_lock(&neighbor_mutex);

    // Chercher le voisin existant
    int found = -1;
    for (int i = 0; i < neighbor_count; i++) {
        if (strcmp(neighbors[i].router_id, router_id) == 0) {
            found = i;
            break;
        }
    }

    if (found >= 0) {
        // Mettre à jour voisin existant
        neighbors[found].last_hello = time(NULL);
        neighbors[found].link_state = 1;
        strcpy(neighbors[found].ip_address, router_ip); // Mettre à jour l'IP
        printf("🔄 Mise à jour voisin: %s (%s)\n", router_id, router_ip);
    } else if (neighbor_count < MAX_NEIGHBORS) {
        // Ajouter nouveau voisin
        strcpy(neighbors[neighbor_count].router_id, router_id);
        strcpy(neighbors[neighbor_count].ip_address, router_ip);
        neighbors[neighbor_count].metric = 1;
        neighbors[neighbor_count].last_hello = time(NULL);
        neighbors[neighbor_count].bandwidth_mbps = 100;
        neighbors[neighbor_count].link_state = 1;

        // CORRECTION 3: Améliorer la détection d'interface
        for (int j = 0; j < interface_count; j++) {
            // Extraire le réseau de l'interface locale
            char local_network[16];
            strcpy(local_network, interfaces[j].ip_address);
            char *dot = strrchr(local_network, '.');
            if (dot) strcpy(dot, ".0");
            
            // Extraire le réseau de l'IP du voisin
            char neighbor_network[16];
            strcpy(neighbor_network, router_ip);
            dot = strrchr(neighbor_network, '.');
            if (dot) strcpy(dot, ".0");
            
            // Si même réseau, utiliser cette interface
            if (strcmp(local_network, neighbor_network) == 0) {
                strcpy(neighbors[neighbor_count].interface, interfaces[j].name);
                break;
            }
        }

        printf("🤝 Nouveau voisin découvert: %s (%s) sur interface %s\n", 
               router_id, router_ip, neighbors[neighbor_count].interface);
        neighbor_count++;
    }

    pthread_mutex_unlock(&neighbor_mutex);
}

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

// CORRECTION 4: Amélioration de la création des LSA
void *lsa_thread(void *arg)
{
    char hostname[256];
    char lsa_message[BUFFER_SIZE * 2]; // Buffer plus grand pour les LSA

    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    while (running)
    {
        pthread_mutex_lock(&neighbor_mutex);

        // Créer LSA avec nos interfaces ET nos voisins
        snprintf(lsa_message, sizeof(lsa_message), "LSA|%s|%d|%d",
                 hostname, (int)time(NULL), neighbor_count + interface_count);

        // Ajouter nos propres interfaces comme "liens"
        for (int i = 0; i < interface_count; i++) {
            char interface_info[128];
            snprintf(interface_info, sizeof(interface_info), "|%s,%s,0,1000",
                     hostname, interfaces[i].ip_address);
            strcat(lsa_message, interface_info);
        }

        // Ajouter les voisins actifs
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

        printf("📡 Envoi LSA: %s\n", lsa_message);
        broadcast_lsa(lsa_message);

        sleep(30);
    }

    pthread_exit(NULL);
}

void lock_all_mutexes()
{
    pthread_mutex_lock(&neighbor_mutex);
    pthread_mutex_lock(&topology_mutex);
    pthread_mutex_lock(&routing_mutex);
}

void unlock_all_mutexes()
{
    pthread_mutex_unlock(&routing_mutex);
    pthread_mutex_unlock(&topology_mutex);
    pthread_mutex_unlock(&neighbor_mutex);
}

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
                    int metric = neighbors[i].metric;
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
                int link_cost = current_lsa->links[i].metric + 1;
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
    pthread_mutex_lock(&routing_mutex);
    route_count = 0;

    for (int i = 0; i < node_count; i++) {
        if (i == source_index || nodes[i].distance == INT_MAX) continue;
        if (strlen(nodes[i].next_hop) == 0) continue;

        lsa_t *router_lsa = NULL;
        for (int j = 0; j < topology_db_size; j++) {
            if (strcmp(topology_db[j].router_id, nodes[i].router_id) == 0) {
                router_lsa = &topology_db[j];
                break;
            }
        }
        if (!router_lsa) continue;

        for (int k = 0; k < router_lsa->num_links; k++) {
            const char *dest_ip = router_lsa->links[k].ip_address;

            // 🔁 Ne pas ajouter une route vers soi-même
            if (strcmp(router_lsa->links[k].router_id, nodes[source_index].router_id) == 0) {
                continue;
            }

            // 🔍 Vérifie que ce n'est pas l'une de nos propres interfaces
            int skip = 0;
            for (int m = 0; m < interface_count; m++) {
                if (strcmp(dest_ip, interfaces[m].ip_address) == 0) {
                    skip = 1;
                    break;
                }
            }
            if (skip) continue;

            // 🔄 Obtenir le /24 réseau à partir de l'IP
            struct in_addr addr;
            if (inet_aton(dest_ip, &addr) == 0) continue;
            uint32_t ip = ntohl(addr.s_addr);
            ip &= 0xFFFFFF00; // masque /24
            addr.s_addr = htonl(ip);
            char dest_network[32];
            snprintf(dest_network, sizeof(dest_network), "%s/24", inet_ntoa(addr));

            // 🔍 S'assurer que ce n’est pas déjà une route locale
            int is_local_network = 0;
            for (int m = 0; m < interface_count; m++) {
                struct in_addr local;
                if (inet_aton(interfaces[m].ip_address, &local) == 0) continue;
                uint32_t lip = ntohl(local.s_addr) & 0xFFFFFF00;
                if (lip == ip) {
                    is_local_network = 1;
                    break;
                }
            }
            if (is_local_network) continue;

            // 🚫 Éviter doublons ou mettre à jour meilleure route
            int found = 0;
            for (int r = 0; r < route_count; r++) {
                if (strcmp(routing_table[r].destination, dest_network) == 0) {
                    if (routing_table[r].metric > (nodes[i].distance + router_lsa->links[k].metric)) {
                        strcpy(routing_table[r].next_hop, nodes[i].next_hop);
                        strcpy(routing_table[r].interface, nodes[i].interface);
                        routing_table[r].metric = nodes[i].distance + router_lsa->links[k].metric;
                        routing_table[r].hop_count = (routing_table[r].metric + 999) / 1000;
                        routing_table[r].bandwidth = router_lsa->links[k].bandwidth_mbps;
                    }
                    found = 1;
                    break;
                }
            }

            if (!found && route_count < MAX_ROUTES) {
                strcpy(routing_table[route_count].destination, dest_network);
                strcpy(routing_table[route_count].next_hop, nodes[i].next_hop);
                strcpy(routing_table[route_count].interface, nodes[i].interface);
                routing_table[route_count].metric = nodes[i].distance + router_lsa->links[k].metric;
                routing_table[route_count].hop_count = (routing_table[route_count].metric + 999) / 1000;
                routing_table[route_count].bandwidth = router_lsa->links[k].bandwidth_mbps;
                route_count++;
            }
        }
    }

    pthread_mutex_unlock(&routing_mutex);

    printf("🔧 DEBUG: %d routes construites\n", route_count);
    for (int i = 0; i < route_count; i++) {
        printf("  Route %d: %s via %s dev %s (métrique: %d)\n", 
               i, routing_table[i].destination, routing_table[i].next_hop, 
               routing_table[i].interface, routing_table[i].metric);
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

    pthread_mutex_lock(&neighbor_mutex);
    for (int i = 0; i < neighbor_count; i++)
    {
        if (neighbors[i].link_state == 1 &&
            strcmp(neighbors[i].ip_address, sender_ip) != 0)
        {

            struct sockaddr_in neighbor_addr;
            memset(&neighbor_addr, 0, sizeof(neighbor_addr));
            neighbor_addr.sin_family = AF_INET;
            neighbor_addr.sin_port = htons(BROADCAST_PORT);
            neighbor_addr.sin_addr.s_addr = inet_addr(neighbors[i].ip_address);

            sendto(flood_sock, lsa_message, strlen(lsa_message), 0,
                   (struct sockaddr *)&neighbor_addr, sizeof(neighbor_addr));
        }
    }
    pthread_mutex_unlock(&neighbor_mutex);
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
    
    // ÉTAPE 1: Découvrir les interfaces réseau EN PREMIER
    printf("🔍 Découverte des interfaces réseau...\n");
    if (discover_interfaces() <= 0)
    {
        printf("❌ Aucune interface réseau découverte\n");
        return 1;
    }
    for (int i = 0; i < interface_count; i++) {
    printf("🌐 Interface %s -> broadcast %s:%d\n", interfaces[i].name, interfaces[i].broadcast_ip, BROADCAST_PORT);
    }    
    printf("=====================================\n\n");


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