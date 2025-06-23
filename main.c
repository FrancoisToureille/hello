#include "config.h"
#include "neighbors.h"
#include "hello.h"
#include "routing_receiver.h"

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

    load_config("router.conf");
    init_neighbors();

    start_hello();

    pthread_t routing_rx;
    pthread_create(&routing_rx, NULL, routing_receiver_thread, NULL);

    while (running) {
        sleep(10);
        cleanup_neighbors();
        print_neighbors();
        print_routing_table();
    }

    pthread_join(routing_rx, NULL);
    return 0;
}
