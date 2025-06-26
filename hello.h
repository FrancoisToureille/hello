#ifndef HELLO_H
#define HELLO_H

void *thread_hello(void *arg);
void processus_message_hello(const char *message, const char *sender_ip);
void supprimer_voisins_down(void);
int voir_interfaces_locales();
void ajouter_routes_locales(void);

#endif