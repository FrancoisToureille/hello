#ifndef CONTROL_SERVER_H
#define CONTROL_SERVER_H

#include <pthread.h>

// Le port est défini dans config.h, donc pas besoin de le redéfinir ici
// #define CONTROL_PORT 9090 ← supprimé

void* control_server_thread(void* arg);
int is_paused();

#endif // CONTROL_SERVER_H
