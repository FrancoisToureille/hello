#include "types.h"
#include "hello.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

// Thread d'envoi périodique des messages HELLO
void *thread_hello(void *arg)
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }

    char hello_message[TAILLE_BUFFER];

    while (en_fonctionnement)
    {
        for (int i = 0; i < nombre_interfaces; i++)
        {
            if (!interfaces[i].active)
                continue;

            snprintf(hello_message, sizeof(hello_message),
                     "HELLO|%s|%s|%ld",
                     hostname, interfaces[i].ip_locale, time(NULL));

            printf("  -> Interface %s (%s) vers %s\n",
                   interfaces[i].nom, interfaces[i].ip_locale, interfaces[i].ip_diffusion);

            int hello_socket = create_broadcast_socket();
            if (hello_socket < 0)
            {
                printf("Echec de la création de la socket Hello\n");
                continue;
            }

            struct sockaddr_in broadcast_addr = {0};
            broadcast_addr.sin_family = AF_INET;
            broadcast_addr.sin_port = htons(PORT_DIFFUSION);
            broadcast_addr.sin_addr.s_addr = inet_addr(interfaces[i].ip_diffusion);

            if (sendto(hello_socket, hello_message, strlen(hello_message), 0,
                       (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0)
            {
                perror("Echec du Hello");
            }
            else
            {
                printf("  ✅ Hello envoyé sur %s\n", interfaces[i].ip_diffusion);
            }

            close(hello_socket);
        }

        supprimerVoisins();
        initialize_own_lsa();
        sleep(INTERVALLE_HELLO);
    }

    pthread_exit(NULL);
}

// Traitement des messages HELLO reçus
void process_hello_message(const char *message, const char *sender_ip)
{
    printf("🔍 Message reçu: %s (de %s)\n", message, sender_ip);

    if (strncmp(message, "HELLO|", 6) != 0)
        return;

    const char *msg_ptr = message + 6;
    const char *router_id_end = strchr(msg_ptr, '|');
    if (!router_id_end)
        return;

    size_t router_id_len = router_id_end - msg_ptr;
    if (router_id_len >= 32)
        return;

    char router_id[32];
    strncpy(router_id, msg_ptr, router_id_len);
    router_id[router_id_len] = '\0';

    printf("📛 router_id extrait: %s\n", router_id);

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown");
    }
    printf("🖥️  Mon hostname local: %s\n", hostname);

    if (strcmp(router_id, hostname) == 0)
    {
        printf("⚠️  HELLO ignoré : vient de moi-même (%s)\n", router_id);
        return;
    }

    const char *ip_start = router_id_end + 1;
    const char *ip_end = strchr(ip_start, '|');
    if (!ip_end)
        return;

    size_t ip_len = ip_end - ip_start;
    if (ip_len >= 32)
        return;

    char router_ip[32];
    strncpy(router_ip, ip_start, ip_len);
    router_ip[ip_len] = '\0';

    printf("🌐 IP du voisin extraite: %s\n", router_ip);

    pthread_mutex_lock(&mutex_voisins);

    int found = -1;
    for (int i = 0; i < nombre_voisins; i++)
    {
        if (strcmp(voisins[i].id_routeur, router_id) == 0)
        {
            found = i;
            break;
        }
    }

    if (found >= 0)
    {
        voisins[found].dernier_hello = time(NULL);
        voisins[found].etat_lien = 1;
        printf("🔄 Mise à jour voisin existant: %s\n", router_id);
    }
    else if (nombre_voisins < NB_MAX_VOISINS)
    {
        printf("➕ Ajout nouveau voisin %s (%s)\n", router_id, router_ip);
        strcpy(voisins[nombre_voisins].id_routeur, router_id);
        strcpy(voisins[nombre_voisins].adresse_ip, router_ip);
        voisins[nombre_voisins].metrique = 1;
        voisins[nombre_voisins].dernier_hello = time(NULL);
        voisins[nombre_voisins].debit_mbps = 100;
        voisins[nombre_voisins].etat_lien = 1;

        int matched = 0;
        for (int j = 0; j < nombre_interfaces; j++)
        {
            printf("🔍 Test interface %s (%s) avec IP %s\n",
                   interfaces[j].nom, interfaces[j].ip_locale, router_ip);

            char *dot_pos = strrchr(interfaces[j].ip_locale, '.');
            if (dot_pos)
            {
                size_t prefix_len = dot_pos - interfaces[j].ip_locale;
                if (strncmp(router_ip, interfaces[j].ip_locale, prefix_len) == 0)
                {
                    strcpy(voisins[nombre_voisins].interface, interfaces[j].nom);
                    matched = 1;
                    break;
                }
            }
        }

        if (!matched)
        {
            strcpy(voisins[nombre_voisins].interface, interfaces[0].nom);
            printf("⚠️  Aucune interface correspondante. Utilisation de %s par défaut.\n",
                   interfaces[0].nom);
        }

        printf("🤝 Nouveau voisin découvert: %s (%s via %s)\n",
               router_id, router_ip, voisins[nombre_voisins].interface);

        nombre_voisins++;
    }
    else
    {
        printf("🚫 Impossible d'ajouter le voisin : MAX atteint.\n");
    }

    pthread_mutex_unlock(&mutex_voisins);
}
