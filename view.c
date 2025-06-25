// view.c
#include "types.h"
#include "view.h"
#include <stdio.h>
#include <pthread.h>

void afficher_voisins()
{
    pthread_mutex_lock(&mutex_voisins);

    printf("\n=== Table des Voisins Actifs ===\n");
    printf("%-15s %-15s %-8s %-12s %-8s\n", "Routeur", "Adresse IP", "Métrique", "Bande Passante", "État");
    printf("--------------------------------------------------------------\n");

    for (int i = 0; i < nombre_voisins; i++)
    {
        printf("%-15s %-15s %-8d %-12d %-8s\n",
               voisins[i].id_routeur,
               voisins[i].adresse_ip,
               voisins[i].metrique,
               voisins[i].debit_mbps,
               voisins[i].etat_lien ? "ACTIF" : "INACTIF");
    }

    pthread_mutex_unlock(&mutex_voisins);
}

void afficher_table_routage()
{
    pthread_mutex_lock(&mutex_routage);

    printf("\n=== Table de Routage ===\n");
    printf("%-18s %-18s %-12s %-8s %-6s\n", "Destination", "Prochain Saut", "Interface", "Métrique", "Sauts");
    printf("---------------------------------------------------------------\n");

    for (int i = 0; i < nombre_routes; i++)
    {
        printf("%-18s %-18s %-12s %-8d %-6d\n",
               table_routage[i].destination,
               table_routage[i].prochain_saut,
               table_routage[i].interface,
               table_routage[i].metrique,
               table_routage[i].nombre_sauts);
    }

    pthread_mutex_unlock(&mutex_routage);
}
