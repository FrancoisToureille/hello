#ifndef LSA_H
#define LSA_H

void *thread_lsa(void *arg);
void processus_lsa(const char *message, const char *sender_ip);
void init_lsa(void);
void lsa_diffusion(const char *lsa_message);
void flow_lsa(const char *lsa_message, const char *sender_ip);

#endif