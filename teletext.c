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
#define OFFSET (8)
#define FIXED (24)
#define ROW(i, n) (i+(PITCH(WIDTH)*(n))+OFFSET)

int height = 32;
uint16_t line_mask[2];


void draw(uint8_t *image, int next_resource)
{
    int m = line_mask[next_resource];
    for (int n = 0; n < height; n += 2) {
        if (!(m & 1)) get_packet(ROW(image, n+next_resource) + FIXED); // skip the fixed clock
        m >>= 1;
    }
}


void init(uint8_t *image)
{
    // initialize image buffer with clock run in
    int n, m, clock = 0x275555;
    int even, odd;
    for (m=0; m<FIXED; m++) {
        even = line_mask[0];
        odd = line_mask[1];
        for (n=0; n<height; n+=2) {
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
    int c, level = 100;
    char *mvalue = NULL;
    char *ovalue = NULL;
    while ((c = getopt(argc,argv,"fl:m:o:")) != -1)
    {
        switch(c)
        {
            case 'f':
                height = 576;
                break;
            case 'l':
                level = strtol(optarg,NULL,0);
                if (level < 0) level = 0;
                if (level > 100) level = 100;
                break;
            case 'm':
                mvalue = optarg;
                break;
            case 'o':
                ovalue = optarg;
                break;
        }
    }

    line_mask[0] = 0; // default to all 16 vbi lines used on both fields
    line_mask[1] = 0;
    
    if (mvalue)
    {
        line_mask[0] = strtol(mvalue,NULL,0);
        if (!ovalue)
            line_mask[1] = line_mask[0];
    }
    if (ovalue)
    {
        line_mask[1] = strtol(ovalue,NULL,0);
        if (!mvalue)
            line_mask[0] = line_mask[1];
    }

    void *render_handle = render_start(WIDTH, height, OFFSET, FIXED, init, draw, -1, level);

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
