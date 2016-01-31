/*
   buffer.c - teletext packet fifo buffer
   Copyright 2015 Alistair Buxton <a.j.buxton@gmail.com>

   This file is part of raspi-teletext.

   raspi-teletext is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   raspi-teletext is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with raspi-teletext. If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#define NBUFFERS 64

uint8_t tt_buffer[NBUFFERS][4];
volatile uint8_t buffer_head = 0;
volatile uint8_t buffer_tail = 0;

const uint8_t fill_buffer[3] = {0x00, 0x00};

void copy_packet(const uint8_t *src, uint8_t *dest)
{
    int n, m;
    for (n=0; n<2; n++) {
        uint8_t b = *src++;
        for (m=0; m<8; m++) {
            *dest++ = b&1;
            *dest++ = b&1;
            b = b>>1;
        }
    }
}

void get_packet(uint8_t *desta, uint8_t *destb)
{
    if (buffer_head == buffer_tail) {
        copy_packet(fill_buffer, desta);
        copy_packet(fill_buffer, destb);
    } else {
        copy_packet(tt_buffer[buffer_tail], desta);
        copy_packet(tt_buffer[buffer_tail]+2, destb);
        buffer_tail = (buffer_tail+1)%NBUFFERS;
    }
}

void push_packet(uint8_t *src)
{
    while (((buffer_head+1)%NBUFFERS) == buffer_tail) {
        usleep(20000);
    }
    memcpy(tt_buffer[buffer_head], src, 4);
    buffer_head = (buffer_head+1)%NBUFFERS;
}

int read_packets(void)
{
    if (((buffer_head+1)%NBUFFERS) == buffer_tail) {
        // wait one field for fifo to empty
        usleep(20000);
        return 1;
    }
    if (fread(tt_buffer[buffer_head], 4, 1, stdin) == 1) {
        buffer_head = (buffer_head+1)%NBUFFERS;
        return 1;
    }
    return 0;
}

