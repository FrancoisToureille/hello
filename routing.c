// routing.c
#include "types.h"
#include "routing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

void construire_table_routage(dijkstra_node_t *noeuds, int nb_noeuds, int index_source) {
    nombre_routes = 0;

    for (int i = 0; i < nb_noeuds; i++) {
        if (i == index_source) continue;

        // Chercher le LSA correspondant dans la base topologique
        for (int j = 0; j < taille_topologie; j++) {
            if (strcmp(topologie_db[j].id_routeur, noeuds[i].id_routeur) != 0) continue;

            // Pour chaque lien du routeur distant
            for (int k = 0; k < topologie_db[j].nb_liens; k++) {
                const char *ip_dest = topologie_db[j].liens[k].adresse_ip;

                // Vérifier que ce n'est pas une de nos propres interfaces
                int est_ip_locale = 0;
                for (int m = 0; m < nombre_interfaces; m++) {
                    if (strcmp(ip_dest, interfaces[m].adresse_ip) == 0) {
                        est_ip_locale = 1;
                        break;
                    }
                }
                if (est_ip_locale)
                    continue;

                // Calculer le préfixe réseau (exemple 10.1.0.0/24)
                char prefixe[32];
                strcpy(prefixe, ip_dest);
                char *dernier_point = strrchr(prefixe, '.');
                if (dernier_point) strcpy(dernier_point + 1, "0/24");

                // Vérifier qu'on ne l'a pas déjà ajouté (éviter doublons)
                int deja_present = 0;
                for (int r = 0; r < nombre_routes; r++) {
                    if (strcmp(table_routage[r].destination, prefixe) == 0) {
                        deja_present = 1;
                        break;
                    }
                }
                if (deja_present)
                    continue;

                // Vérifier que ce n'est pas un réseau local à nous
                int est_reseau_local = 0;
                for (int m = 0; m < nombre_interfaces; m++) {
                    char prefixe_local[32];
                    strcpy(prefixe_local, interfaces[m].adresse_ip);
                    char *dernier_point = strrchr(prefixe_local, '.');
                    if (dernier_point) strcpy(dernier_point + 1, "0/24");
                    if (strcmp(prefixe, prefixe_local) == 0) {
                        est_reseau_local = 1;
                        break;
                    }
                }
                if (est_reseau_local)
                    continue;

                // Ajouter la route
                if (nombre_routes < MAX_ROUTES) {
                    strcpy(table_routage[nombre_routes].destination, prefixe);
                    strcpy(table_routage[nombre_routes].prochain_saut, noeuds[i].prochain_saut);
                    strcpy(table_routage[nombre_routes].interface, noeuds[i].interface);
                    table_routage[nombre_routes].metrique = noeuds[i].distance + topologie_db[j].liens[k].metrique;
                    table_routage[nombre_routes].nombre_sauts = (table_routage[nombre_routes].metrique + 999) / 1000;
                    table_routage[nombre_routes].debit = topologie_db[j].liens[k].debit_mbps;
                    nombre_routes++;
                }
            }
        }
    }
}

void mettre_a_jour_table_kernel()
{
    // Supprimer uniquement les routes non locales (next-hop différent de 0.0.0.0)
    system("ip route flush table 100");
    pthread_mutex_lock(&mutex_routage);

    for (int i = 0; i < nombre_routes; i++)
    {
        char commande[256];
        if (strcmp(table_routage[i].prochain_saut, "0.0.0.0") == 0) {
            continue;
        }
        snprintf(commande, sizeof(commande),
                 "ip route replace %s via %s dev %s",
                 table_routage[i].destination,
                 table_routage[i].prochain_saut,
                 table_routage[i].interface);
        printf("🛣️  Route OSPF ajoutée : %s\n", commande);
        int retour = system(commande);
        if (retour != 0) {
            printf("❌ Échec de l'ajout de la route : %s\n", commande);
        }
    }

    pthread_mutex_unlock(&mutex_routage);
}

void supprimer_voisins_expires()
{
    pthread_mutex_lock(&mutex_voisins);
    time_t maintenant = time(NULL);

    for (int i = 0; i < nombre_voisins; i++)
    {
        if (maintenant - voisins[i].dernier_hello > DELAI_EXPIRATION_VOISINS)
        {
            printf("❌ Voisin expiré détecté : %s\n", voisins[i].id_routeur);
            voisins[i].etat_lien = 0;
        }
    }

    pthread_mutex_unlock(&mutex_voisins);
}
