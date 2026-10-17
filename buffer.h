#include <stdio.h>
#include <stdint.h>
void get_packet(uint8_t *dest);
void push_packet(uint8_t *src);
int read_packets(void);
int read_packets_from(FILE *f);
void wait_packets_sent(void);

