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

#define NBUFFERS 64

uint8_t tt_buffer[NBUFFERS][42];
volatile uint8_t buffer_head = 0;
volatile uint8_t buffer_tail = 0;

const uint8_t fill_buffer[42] = {
  0x49, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x20, 0x31, 0xb0, 0xb0, 0x20, 0x54, 0x75, 0xe5,
  0x20, 0x31, 0x37, 0x20, 0x4a, 0x61, 0x6e, 0x83,
  0x32, 0x31, 0xba, 0xb5, 0xb0, 0x2f, 0xb3, 0xb6
//  packet 0 header - for debugging
//  0x02, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
//  0x52, 0x61, 0x73, 0x70, 0x62, 0xe5, 0xf2, 0xf2,
//  0x79, 0xd0, 0xe9, 0x20, 0xcd, 0xef, 0x6e, 0x20,
//  0xb0, 0xb9, 0x20, 0x46, 0xe5, 0x62, 0x20, 0xb0,
//  0x32, 0xba, 0x31, 0x38, 0x2f, 0xb5, 0xb9, 0x80
};


void copy_packet(const uint8_t *src, uint8_t *dest)
{
    int n, m;
    for (n=0; n<42; n++) {
        uint8_t b = *src++;
        for (m=0; m<8; m++) {
            *dest++ = b&1;
            b = b>>1;
        }
    }
}

void get_packet(uint8_t *dest)
{
    if (buffer_head == buffer_tail) {
        copy_packet(fill_buffer, dest);
    } else {
        copy_packet(tt_buffer[buffer_tail], dest);
        buffer_tail = (buffer_tail+1)%NBUFFERS;
    }
}

int read_packets(void)
{
    if (((buffer_head+1)%NBUFFERS) == buffer_tail) {
        // wait one field for fifo to empty
        usleep(20000);
        return 1;
    }
    if (fread(tt_buffer[buffer_head], 42, 1, stdin) == 1) {
        buffer_head = (buffer_head+1)%NBUFFERS;
        return 1;
    }
    return 0;
}

