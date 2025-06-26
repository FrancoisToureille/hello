#ifndef CONTROL_H
#define CONTROL_H

void gestion_signal(int sig);
void *thread_ecoute(void *arg);
int creer_socket_diffusion(void);
int envoyer_message(const char *message);

#endif