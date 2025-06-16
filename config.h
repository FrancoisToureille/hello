#pragma once

#define MAX_INTERFACES 10
#define MAX_NAME_LEN 32
#define MAX_NEIGHBORS 32
#define HELLO_PORT 5000
#define HELLO_INTERVAL 5
#define TIMEOUT_NEIGHBOR 15

extern char router_id[MAX_NAME_LEN];
extern char interfaces[MAX_INTERFACES][MAX_NAME_LEN];
extern int interface_count;

void load_config(const char* filename);
