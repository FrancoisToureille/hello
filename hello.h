#ifndef HELLO_H
#define HELLO_H

#define HELLO_PORT 12345
#define HELLO_INTERVAL 5

void start_hello();
void send_network_list(const char* dest_ip);  // <- ajout nécessaire

#endif
