#pragma once
void *thread_hello(void *arg);
void process_hello_message(const char *message, const char *sender_ip);
void supprimerVoisins(void);
int create_broadcast_socket(void);