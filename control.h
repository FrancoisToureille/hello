#pragma once
void signal_handler(int sig);
void lock_all_mutexes(void);
void unlock_all_mutexes(void);
void *listen_thread(void *arg);
int send_message(const char *message);
int discover_interfaces(void);
void ensure_local_routes(void);
