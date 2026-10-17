#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

int vd_get_line(uint8_t *dest);
void vd_push_bytes(const uint8_t *src, size_t len);
void vd_push_flush(void);
int vd_read_lines(void);
int vd_read_lines_from(FILE *f);
void vd_wait_empty(void);
