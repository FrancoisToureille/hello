#ifndef CONTROL_SERVER_H
#define CONTROL_SERVER_H

void* control_server_thread(void* arg);
int is_paused();
void stop_control_server();  // ← ajouté

#endif
