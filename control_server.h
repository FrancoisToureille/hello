// control_server.h
#ifndef CONTROL_SERVER_H
#define CONTROL_SERVER_H

#define CONTROL_PORT 9090

void* control_server_thread(void* arg);
int is_paused();

#endif
