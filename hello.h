#ifndef HELLO_H
#define HELLO_H

void *hello_thread(void *arg);
void process_hello_message(const char *message, const char *sender_ip);
void cleanup_expired_neighbors(void);
int discover_interfaces();
void ensure_local_routes(void);

#endif