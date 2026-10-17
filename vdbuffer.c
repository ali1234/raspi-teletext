/*
   vdbuffer.c - Channel VideoDat line fifo buffer

   This file is part of raspi-teletext.

   raspi-teletext is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#include "vdbuffer.h"
#include "vdline.h"

#define NBUFFERS 256

static uint8_t vd_buffer[NBUFFERS][VD_LINE_BYTES];
static volatile uint16_t buffer_head = 0;
static volatile uint16_t buffer_tail = 0;

// bytes of the line being filled by vd_push_bytes
static uint8_t partial[VD_LINE_BYTES];
static size_t npartial = 0;


// Returns 0 if there is no data, then the line stays blank.
int vd_get_line(uint8_t *dest)
{
    if (buffer_head == buffer_tail)
        return 0;
    memcpy(dest, vd_buffer[buffer_tail], VD_LINE_BYTES);
    buffer_tail = (buffer_tail+1)%NBUFFERS;
    return 1;
}

static void push_line(const uint8_t *src)
{
    while (((buffer_head+1)%NBUFFERS) == buffer_tail) {
        usleep(20000);
    }
    memcpy(vd_buffer[buffer_head], src, VD_LINE_BYTES);
    buffer_head = (buffer_head+1)%NBUFFERS;
}

// Packets don't line up with VBI lines, so collect bytes into lines.
void vd_push_bytes(const uint8_t *src, size_t len)
{
    while (len--) {
        partial[npartial++] = *src++;
        if (npartial == VD_LINE_BYTES) {
            push_line(partial);
            npartial = 0;
        }
    }
}

// Pad the last line with FF, which can't start a sync word.
void vd_push_flush(void)
{
    if (npartial) {
        memset(partial + npartial, 0xff, VD_LINE_BYTES - npartial);
        push_line(partial);
        npartial = 0;
    }
}

int vd_read_lines(void)
{
    return vd_read_lines_from(stdin);
}

int vd_read_lines_from(FILE *f)
{
    uint8_t buf[VD_LINE_BYTES];
    size_t n = fread(buf, 1, VD_LINE_BYTES, f);
    vd_push_bytes(buf, n);
    return n == VD_LINE_BYTES;
}

void vd_wait_empty(void)
{
    while (buffer_head != buffer_tail) {
        usleep(20000);
    }
}
