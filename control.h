#ifndef CONTROL_H
#define CONTROL_H

void signal_handler(int sig);
void *listen_thread(void *arg);
int create_broadcast_socket(void);
int send_message(const char *message);

#endif