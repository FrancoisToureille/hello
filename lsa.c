// lsa.c
#include "types.h"
#include "lsa.h"
#include "routing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

// Thread pour échanger les LSA
void *lsa_thread(void *arg)
{
    char hostname[256];
    char lsa_message[TAILLE_BUFFER];

    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    while (en_fonctionnement)
    {
        // Créer et envoyer notre LSA avec toutes nos interfaces
        pthread_mutex_lock(&mutex_voisins);

        // Construire le LSA avec nos voisins et nos interfaces locales
        snprintf(lsa_message, sizeof(lsa_message), "LSA|%s|%d|%d",
                 hostname, (int)time(NULL), nombre_voisins + nombre_interfaces);

        // Ajouter les informations de chaque voisin
        for (int i = 0; i < nombre_voisins; i++)
        {
            if (voisins[i].etat_lien == 1)
            {
                char link_info[128];
                snprintf(link_info, sizeof(link_info), "|%s,%s,%d,%d",
                         voisins[i].id_routeur, voisins[i].adresse_ip,
                         voisins[i].metrique, voisins[i].debit_mbps);
                strcat(lsa_message, link_info);
            }
        }
        
        // Ajouter nos propres interfaces au LSA
        for (int i = 0; i < nombre_interfaces; i++)
        {
            if (interfaces[i].active)
            {
                char interface_info[128];
                snprintf(interface_info, sizeof(interface_info), "|%s,%s,%d,%d",
                         hostname, interfaces[i].ip_locale, 1, 1000);
                strcat(lsa_message, interface_info);
            }
        }

        pthread_mutex_unlock(&mutex_voisins);

        // Envoyer LSA sur toutes les interfaces
        broadcast_lsa(lsa_message);
        printf("📡 LSA diffusé: %d voisins + %d interfaces\n", nombre_voisins, nombre_interfaces);

        sleep(30); // Envoyer LSA toutes les 30 secondes
    }

    pthread_exit(NULL);
}

void flood_lsa(const char *lsa_message, const char *sender_ip)
{
    int flood_sock = create_broadcast_socket();
    if (flood_sock < 0)
        return;

    pthread_mutex_lock(&mutex_voisins);
    for (int i = 0; i < nombre_voisins; i++)
    {
        if (voisins[i].etat_lien == 1 &&
            strcmp(voisins[i].adresse_ip, sender_ip) != 0)
        {
            struct sockaddr_in neighbor_addr;
            memset(&neighbor_addr, 0, sizeof(neighbor_addr));
            neighbor_addr.sin_family = AF_INET;
            neighbor_addr.sin_port = htons(PORT_DIFFUSION);
            neighbor_addr.sin_addr.s_addr = inet_addr(voisins[i].adresse_ip);

            sendto(flood_sock, lsa_message, strlen(lsa_message), 0,
                   (struct sockaddr *)&neighbor_addr, sizeof(neighbor_addr));
        }
    }
    pthread_mutex_unlock(&mutex_voisins);
    close(flood_sock);
}

// Fonction pour traiter les messages LSA
void process_lsa_message(const char *message, const char *sender_ip)
{
    // Format: LSA|id_routeur|horodatage|nb_liens|id_routeur,ip,metrique,debit|...
    char msg_copy[TAILLE_BUFFER];
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

    int lsa_num_links = atoi(lsa_num_links_str);

    lsa_t new_lsa;
    memset(&new_lsa, 0, sizeof(new_lsa));
    strncpy(new_lsa.id_routeur, lsa_router_id, sizeof(new_lsa.id_routeur) - 1);
    new_lsa.horodatage = atoi(lsa_timestamp_str);
    new_lsa.nb_liens = 0;

    // Parser tous les liens du LSA
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
            
        voisin_t link;
        memset(&link, 0, sizeof(link));
        strncpy(link.id_routeur, router_id, sizeof(link.id_routeur) - 1);
        strncpy(link.adresse_ip, ip, sizeof(link.adresse_ip) - 1);
        link.metrique = atoi(metric_str);
        link.debit_mbps = atoi(bw_str);
        link.etat_lien = 1;
        
        // Déterminer l'interface pour ce lien
        for (int j = 0; j < nombre_interfaces; j++) {
            // Vérifier si l'IP est sur le même réseau que cette interface (classe C)
            char interface_network[32], link_network[32];
            strcpy(interface_network, interfaces[j].ip_locale);
            strcpy(link_network, ip);
            
            char *dot = strrchr(interface_network, '.');
            if (dot) strcpy(dot + 1, "0");
            dot = strrchr(link_network, '.');
            if (dot) strcpy(dot + 1, "0");
            
            if (strcmp(interface_network, link_network) == 0) {
                strncpy(link.interface, interfaces[j].nom, sizeof(link.interface) - 1);
                break;
            }
        }
        
        if (new_lsa.nb_liens < NB_MAX_VOISINS)
            new_lsa.liens[new_lsa.nb_liens++] = link;
    }
    
    int updated = 0;
    pthread_mutex_lock(&mutex_topologie);
    
    int found = -1;
    for (int i = 0; i < taille_topologie; i++)
    {
        if (strcmp(base_topologie[i].id_routeur, new_lsa.id_routeur) == 0)
        {
            // Remplacer si horodatage plus récent
            if (new_lsa.horodatage > base_topologie[i].horodatage)
            {
                base_topologie[i] = new_lsa;
                updated = 1;
            }
            found = 1;
            break;
        }
    }
    
    if (found == -1 && taille_topologie < NB_MAX_TOPOLOGIE)
    {
        base_topologie[taille_topologie++] = new_lsa;
        updated = 1;
    }
    
    pthread_mutex_unlock(&mutex_topologie);

    // Recalculer et flooder seulement si la base topologie a changé
    if (updated)
    {
        printf("📊 LSA mis à jour pour %s (%d liens)\n", lsa_router_id, lsa_num_links);
        calculate_shortest_paths();
        
        // Vérifier si ce n'est pas notre propre LSA avant de flooder
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) != 0)
            strcpy(hostname, "Unknown");
            
        if (strcmp(lsa_router_id, hostname) != 0) {
            flood_lsa(message, sender_ip);
        }
    }
}

// Fonction pour initialiser notre propre LSA dans la base de topologie
void initialize_own_lsa()
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    pthread_mutex_lock(&mutex_topologie);

    int our_lsa_index = -1;
    for (int i = 0; i < taille_topologie; i++)
    {
        if (strcmp(base_topologie[i].id_routeur, hostname) == 0)
        {
            our_lsa_index = i;
            break;
        }
    }

    if (our_lsa_index < 0 && taille_topologie < NB_MAX_TOPOLOGIE)
    {
        our_lsa_index = taille_topologie;
        taille_topologie++;
    }

    if (our_lsa_index >= 0)
    {
        strcpy(base_topologie[our_lsa_index].id_routeur, hostname);
        base_topologie[our_lsa_index].horodatage = (int)time(NULL);
        base_topologie[our_lsa_index].nb_liens = 0;

        pthread_mutex_lock(&mutex_voisins);
        for (int i = 0; i < nombre_voisins && base_topologie[our_lsa_index].nb_liens < NB_MAX_VOISINS; i++)
        {
            if (voisins[i].etat_lien == 1)
            {
                int link_idx = base_topologie[our_lsa_index].nb_liens;
                base_topologie[our_lsa_index].liens[link_idx] = voisins[i];
                base_topologie[our_lsa_index].nb_liens++;
            }
        }
        pthread_mutex_unlock(&mutex_voisins);
        
        // Ajouter nos interfaces actives comme liens dans le LSA local
        for (int i = 0; i < nombre_interfaces && base_topologie[our_lsa_index].nb_liens < NB_MAX_VOISINS; i++)
        {
            if (interfaces[i].active)
            {
                int link_idx = base_topologie[our_lsa_index].nb_liens;
                voisin_t interface_link;
                memset(&interface_link, 0, sizeof(interface_link));
                
                strcpy(interface_link.id_routeur, hostname);
                strcpy(interface_link.adresse_ip, interfaces[i].ip_locale);
                strncpy(interface_link.interface, interfaces[i].nom, sizeof(interface_link.interface) - 1);
                interface_link.metrique = 1;
                interface_link.debit_mbps = 1000;
                interface_link.etat_lien = 1;
                interface_link.dernier_hello = time(NULL);
                
                base_topologie[our_lsa_index].liens[link_idx] = interface_link;
                base_topologie[our_lsa_index].nb_liens++;
            }
        }
    }

    pthread_mutex_unlock(&mutex_topologie);
}

// Fonction pour afficher la table des voisins
void show_neighbors()
{
    pthread_mutex_lock(&mutex_voisins);

    printf("\n=== Table des voisins ===\n");
    printf("%-15s %-15s %-8s %-10s %-8s\n", "Routeur", "IP", "Métrique", "Bande Pass.", "État");
    printf("--------------------------------------------------------\n");

    for (int i = 0; i < nombre_voisins; i++)
    {
        printf("%-15s %-15s %-8d %-10d %-8s\n",
               voisins[i].id_routeur,
               voisins[i].adresse_ip,
               voisins[i].metrique,
               voisins[i].debit_mbps,
               voisins[i].etat_lien ? "UP" : "DOWN");
    }

    pthread_mutex_unlock(&mutex_voisins);
}

// Fonction pour diffuser le LSA sur toutes les interfaces actives
void broadcast_lsa(const char *lsa_message)
{
    struct sockaddr_in broadcast_addr;

    for (int i = 0; i < nombre_interfaces; i++)
    {
        if (interfaces[i].active)
        {
            memset(&broadcast_addr, 0, sizeof(broadcast_addr));
            broadcast_addr.sin_family = AF_INET;
            broadcast_addr.sin_port = htons(PORT_DIFFUSION);
            broadcast_addr.sin_addr.s_addr = inet_addr(interfaces[i].ip_diffusion);

            int sent_bytes = sendto(socket_diffusion, lsa_message, strlen(lsa_message), 0,
                                    (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
            if (sent_bytes < 0)
            {
                perror("Erreur envoi LSA");
            }
        }
    }
}

int create_broadcast_socket()
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return -1;
    }

    int broadcast_enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0)
    {
        perror("setsockopt SO_BROADCAST");
        close(sock);
        return -1;
    }

    return sock;
}
