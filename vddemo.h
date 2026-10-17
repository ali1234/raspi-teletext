#ifndef VDDEMO_H
#define VDDEMO_H

#include <stdint.h>

// Send a small text file over and over, with the clock every 5 seconds.
// Never returns.
void vd_demo(uint32_t seed);

#endif
