#include "config.h"
#include "neighbors.h"
#include "hello.h"
#include "routing_receiver.h"
#include "control_server.h"

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static volatile int running = 1;

void stop(int sig) {
    running = 0;
}

int main() {
    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    // Chargement de la configuration
    load_config("router.conf");
    init_neighbors();

    // Lancement des threads
    start_hello();

    pthread_t routing_rx;
    pthread_create(&routing_rx, NULL, routing_receiver_thread, NULL);

    pthread_t ctrl_thread;
    pthread_create(&ctrl_thread, NULL, control_server_thread, NULL);

    // Boucle principale
    while (running) {
        sleep(10);

        if (is_paused()) {
            printf("[PAUSE] Exécution suspendue.\n");
            continue;
        }

        cleanup_neighbors();
        print_neighbors();
        print_routing_table();
    }

    // Nettoyage
    pthread_join(routing_rx, NULL);
    pthread_join(ctrl_thread, NULL);
    return 0;
}
