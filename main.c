#include "config.h"
#include "neighbors.h"
#include "hello.h"
#include "routing.h"
#include "control_server.h"

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

static volatile int running = 1;

void stop(int sig) {
    running = 0;
}

int main() {
    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    load_config("router.conf");
    init_neighbors();
    init_routing_table();

    pthread_t ctrl_thread;
    pthread_create(&ctrl_thread, NULL, control_server_thread, NULL);

    start_hello();  // Lance les threads hello_sender et hello_receiver

    int routing_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (routing_sock < 0) {
        perror("socket routing");
        return 1;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(ROUTING_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    if (bind(routing_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind routing_sock");
        close(routing_sock);
        return 1;
    }

    while (running) {
        if (!is_paused()) {
            struct sockaddr_in sender;
            socklen_t len = sizeof(sender);
            char buf[512];

            int n = recvfrom(routing_sock, buf, sizeof(buf) - 1, MSG_DONTWAIT,
                             (struct sockaddr*)&sender, &len);
            if (n > 0) {
                buf[n] = '\0';
                process_routing_message(buf);
            }

            cleanup_neighbors();
            print_neighbors();
            print_routing_table();
        }
        sleep(1);
    }

    close(routing_sock);
    return 0;
}
