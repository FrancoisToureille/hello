#include "config.h"
#include "neighbors.h"
#include "hello.h"
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile int running = 1;
void stop(int sig) { running = 0; }

int main() {
    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    load_config("router.conf");
    init_neighbors();
    start_hello();

    while (running) {
    sleep(10);
    cleanup_neighbors();
    print_neighbors();
    }


    return 0;
}
