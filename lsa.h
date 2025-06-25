#ifndef LSA_H
#define LSA_H

void *lsa_thread(void *arg);
void process_lsa_message(const char *message, const char *sender_ip);
void initialize_own_lsa(void);
void broadcast_lsa(const char *lsa_message);
void flood_lsa(const char *lsa_message, const char *sender_ip);

#endif