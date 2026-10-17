/*
   vdline.c - Channel VideoDat line coding

   This file is part of raspi-teletext.

   raspi-teletext is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#include <math.h>

#include "vdline.h"


void vd_line_bits(const uint8_t *bytes, uint8_t *bits)
{
    for (int n = 0; n < VD_LINE_BYTES; n++) {
        uint8_t b = bytes[n];
        *bits++ = 1;                    // start bit
        for (int m = 0; m < 8; m++) {
            *bits++ = !(b & 1);         // data, inverted, LSB first
            b >>= 1;
        }
        *bits++ = 0;                    // stop bit
    }
}


void vd_line_pixels(const uint8_t *bytes, uint8_t *pixels, int npixels, double start, double ppb)
{
    uint8_t bits[VD_LINE_BITS];
    vd_line_bits(bytes, bits);
    // Each pixel shows the bit at its centre. With 6.75 pixels per bit
    // three in four bits are 7 pixels wide and the fourth 6.
    for (int x = 0; x < npixels; x++) {
        double b = floor((x + 0.5 - start) / ppb);
        pixels[x] = (b >= 0 && b < VD_LINE_BITS) ? bits[(int)b] : 0;
    }
}
