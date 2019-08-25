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
#include "buffer.h"
#include "demo.h"

#define WIDTH (370)
#define HEIGHT (32)
#define OFFSET (8)
#define FIXED (24)
#define ROW(i, n) (i+(PITCH(WIDTH)*(n))+OFFSET)

uint16_t mask_even;
uint16_t mask_odd;


void draw(uint8_t *image, int next_resource)
{
    int n;
    int m;
    if(next_resource == 0) {
        m = mask_even;
        for (n = 0; n < HEIGHT; n += 2) {
            if (!(m & 1)) get_packet(ROW(image, n) + FIXED); // +24 because clock never changes
            m >>= 1;
        }
    }
    else {
        m = mask_odd;
        for (n=0; n<HEIGHT; n+=2) {
            if (!(m&1)) get_packet(ROW(image, n+1) + FIXED);
            m >>= 1;
        }
    }
}


void init(uint8_t *image)
{
    // initialize image buffer with clock run in
    int n, m, clock = 0x275555;
    int even, odd;
    for (m=0; m<FIXED; m++) {
        even = mask_even;
        odd = mask_odd;
        for (n=0; n<HEIGHT; n+=2) {
            if (!(even&1)) ROW(image, n)[m] = clock&1;
            if (!(odd&1)) ROW(image, n+1)[m] = clock&1;
            even >>= 1;
            odd >>= 1;
        }
        clock = clock >> 1;
    }
    draw(image, 0); // load first field in to image
}


int main(int argc, char *argv[])
{
    int c;
    char *mvalue = NULL;
    char *ovalue = NULL;
    while ((c = getopt(argc,argv,"m:o:")) != -1)
    {
        switch(c)
        {
            case 'm':
                mvalue = optarg;
                break;
            case 'o':
                ovalue = optarg;
                break;
        }
    }

    mask_even = 0; // default to all 16 vbi lines used on both fields
    mask_odd = 0;
    
    if (mvalue)
    {
        mask_even = strtol(mvalue,NULL,0);
        if (!ovalue)
            mask_odd = mask_even;
    }
    if (ovalue)
    {
        mask_odd = strtol(ovalue,NULL,0);
        if (!mvalue)
            mask_even = mask_odd;
    }

    void *render_handle = render_start(WIDTH, HEIGHT, OFFSET, FIXED, init, draw, -1);

    if (argc >= 2 && strlen(argv[argc-1])==1 && argv[argc-1][0] == '-') { // last argument is a single '-'
        while(read_packets()) {
            ;
        }
    } else {
        demo();
    }

    render_stop(render_handle);

    return 0;
}
