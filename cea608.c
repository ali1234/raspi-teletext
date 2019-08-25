/*
   main.c - dispmanx teletext display
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
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>

#include "bcm_host.h"

#include "render.h"
#include "cea608buffer.h"

#define WIDTH (53)
#define HEIGHT (2)
#define OFFSET (1)
#define FIXED (19)
#define ROW(i, n) (i+(PITCH(WIDTH)*(n))+OFFSET)


static uint8_t hello[] = {
    0x94, 0x20, 0x00, 0x00,
    0x68, 0xe5, 0x00, 0x00,
    0xec, 0xec, 0x00, 0x00,
    0xef, 0xa1, 0x00, 0x00,
    0x94, 0x2f, 0x00, 0x00
};


void draw(uint8_t *image, int next_resource)
{
    // fill image
    if(next_resource == 0) {
        get_packet(ROW(image, 0)+FIXED, ROW(image, 1)+FIXED);
    }
}


void init(uint8_t *image)
{
    // initialize image buffer with clock run in
    int n, m, clock = 0x61555;
    for (m=0; m<FIXED; m++) {
        for (n=0; n<HEIGHT; n++) {
            ROW(image, n)[m] = clock&1;
        }
        clock = clock >> 1;
    }
    draw(image, 0); // load first field in to image
}


int main(int argc, char *argv[])
{
    void *render_handle = render_start(WIDTH, HEIGHT, OFFSET, FIXED, init, draw, -1);

    if (argc >= 2 && strlen(argv[argc-1])==1 && argv[argc-1][0] == '-') { // last argument is a single '-'
        while(read_packets()) {
            ;
        }
    } else {
        while(1) {
            for(int n=0;n<20;n+=4) {
                push_packet(hello+n);
            }
        }
    }

    render_stop(render_handle);

    return 0;
}
