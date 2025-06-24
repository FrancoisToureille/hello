#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char router_id[MAX_NAME_LEN];
char interfaces[MAX_INTERFACES][MAX_NAME_LEN];
int interface_count = 0;
char broadcasts[MAX_INTERFACES][INET_ADDRSTRLEN] = {{0}};

void load_config(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        perror("router.conf");
        exit(EXIT_FAILURE);
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "router_id", 9) == 0) {
            sscanf(line, "router_id = %s", router_id);
        } else if (strncmp(line, "interfaces", 10) == 0) {
            char* token = strchr(line, '=') + 1;
            char* ifname = strtok(token, ", \n");
            while (ifname && interface_count < MAX_INTERFACES) {
                strncpy(interfaces[interface_count++], ifname, MAX_NAME_LEN);
                ifname = strtok(NULL, ", \n");
            }
        }

        if (strncmp(line, "broadcast.", 10) == 0) {
            char ifname[MAX_NAME_LEN];
            char ip[INET_ADDRSTRLEN];
            if (sscanf(line, "broadcast.%[^=] = %s", ifname, ip) == 2) {
                for (int i = 0; i < interface_count; ++i) {
                    if (strcmp(interfaces[i], ifname) == 0) {
                        strncpy(broadcasts[i], ip, INET_ADDRSTRLEN);
                        break;
                    }
                }
            }
        }
    }

    fclose(f);
}
