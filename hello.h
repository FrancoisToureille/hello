#pragma once
void *thread_hello(void *arg);
void process_hello_message(const char *message, const char *sender_ip);
void cleanup_expired_neighbors(void);
int create_broadcast_socket(void);