#pragma once
void *hello_thread(void *arg);
void process_hello_message(const char *message, const char *sender_ip);
void cleanup_expired_neighbors(void);
int create_broadcast_socket(void);