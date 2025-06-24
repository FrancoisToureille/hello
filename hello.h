#ifndef HELLO_H
#define HELLO_H

#include "config.h"  // pour HELLO_PORT, HELLO_INTERVAL

void start_hello();  // Lance les threads hello_sender + hello_receiver
void send_network_list(const char* dest_ip);  // Envoie les routes locales à un voisin

#endif
