/* Hamming and parity calculations */

/* Copyright 2015 Alistair Buxton <a.j.buxton@gmail.com> */

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

uint8_t hamming84(uint8_t d)
{

    uint8_t d1 = d&1;
    uint8_t d2 = (d>>1)&1;
    uint8_t d3 = (d>>2)&1;
    uint8_t d4 = (d>>3)&1;

    uint8_t p1 = (1 + d1 + d3 + d4) & 1;
    uint8_t p2 = (1 + d1 + d2 + d4) & 1;
    uint8_t p3 = (1 + d1 + d2 + d3) & 1;
    uint8_t p4 = (1 + p1 + d1 + p2 + d2 + p3 + d3 + d4) & 1;

    return (p1 | (d1<<1) | (p2<<2) | (d2<<3)
     | (p3<<4) | (d3<<5) | (p4<<6) | (d4<<7));

}

uint8_t parity(uint8_t d)
{

    d &= 0x7f;
    uint8_t p = 1;
    uint8_t t = d;
    int i;
    for (i=0; i<7; i++) {
        p += t&1;
        t = t>>1;
    }
    p &= 1;
    return d|(p<<7);

}

void str_parity(uint8_t *str, size_t size, const char *format, ...)
{
    va_list argList;
    uint8_t tmp[65];

    if (size > 64) size = 64;

    va_start(argList, format);
    vsnprintf((char *)tmp, size+1, format, argList);
    va_end(argList);

    int n;
    for ( n=0; n<size; n++) {
        str[n] = parity(tmp[n]);
    }
}
