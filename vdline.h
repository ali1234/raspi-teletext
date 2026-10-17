/*
   vdline.h - Channel VideoDat line coding

   This file is part of raspi-teletext.

   raspi-teletext is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#ifndef VDLINE_H
#define VDLINE_H

#include <stdint.h>

// Each VBI line carries 10 bytes as NRZ at 2 Mbit/s. Every byte is sent
// like an inverted UART frame: a white start bit, the 8 data bits LSB
// first with 1 = black, and a black stop bit.
#define VD_LINE_BYTES (10)
#define VD_LINE_BITS (VD_LINE_BYTES * 10)
#define VD_BIT_RATE (2.0e6)

// bits[100]: 1 = white, 0 = black
void vd_line_bits(const uint8_t *bytes, uint8_t *bits);

// Draw a line into pixels[npixels] (1 = white, 0 = black). The first bit
// starts at pixel position start, each bit is ppb pixels wide. Pixels
// outside the data are black.
void vd_line_pixels(const uint8_t *bytes, uint8_t *pixels, int npixels, double start, double ppb);

#endif
