/*
   videodat.c - Channel VideoDat VBI output

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

// Channel VideoDat was sent by Pro7 from 1990 to 1994 in PAL lines 11, 12
// and 13 of the first field and 324, 325 and 326 of the second, 10 bytes
// per line at 2 Mbit/s. See vdline.c for the bit coding and vdpacket.c
// for the packets.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "render.h"
#include "vdbuffer.h"
#include "vddemo.h"
#include "vdline.h"

// One image pixel per output pixel (13.5 MHz), so a bit is 6.75 pixels.
#define WIDTH (720)
#define PIXELS_PER_BIT (13.5e6 / VD_BIT_RATE)
#define ROW(i, n) (i+(PITCH(WIDTH)*(n)))
#define VBI_LINES (16)
#define HEIGHT (2*VBI_LINES)

// The first VBI line output is line 7 (field 1) or 320 (field 2), so bit n
// of a mask is line 7+n or 320+n. Default: lines 11-13 and 324-326.
#define DEFAULT_MASK (0x0070)

uint16_t line_mask[2];
// Position of the first start bit in pixels. 44 puts it where it is in
// recordings of Pro7, compared on a capture of the output. The data then
// ends at pixel 718, just inside the 720 pixel line.
double start = 44;


void draw(uint8_t *image, int next_resource)
{
    uint8_t bytes[VD_LINE_BYTES];
    int m = line_mask[next_resource];
    for (int n = 0; n < VBI_LINES; n++) {
        uint8_t *row = ROW(image, 2*n+next_resource);
        if ((m & 1) && vd_get_line(bytes))
            vd_line_pixels(bytes, row, WIDTH, start, PIXELS_PER_BIT);
        else
            memset(row, 0, WIDTH);
        m >>= 1;
    }
}


void init(uint8_t *image)
{
    draw(image, 0);
}


int main(int argc, char *argv[])
{
    int c, level = 100;
    char *mvalue = NULL;
    char *ovalue = NULL;
    uint32_t seed = time(NULL);
    while ((c = getopt(argc,argv,"l:m:o:s:x:")) != -1)
    {
        switch(c)
        {
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
            case 's':
                seed = strtoul(optarg,NULL,0);
                break;
            case 'x':
                start = strtod(optarg,NULL);
                break;
            default:
                fprintf(stderr, "Usage: %s [-m first field line mask, eg 0x0070] [-o second field line mask] [-l level] [-x start] [-s seed] [-]\n", argv[0]);
                return 1;
        }
    }

    line_mask[0] = DEFAULT_MASK;
    line_mask[1] = DEFAULT_MASK;

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

    void *render_handle = render_start(WIDTH, HEIGHT, 0, 0, init, draw, -1, level, VBI_LINES, "PAL");

    if (optind < argc && strcmp(argv[argc-1], "-") == 0) { // last argument is a single '-'
        while (vd_read_lines()) {
            ;
        }
        vd_push_flush();
        vd_wait_empty();
        usleep(100000); // let the last lines go out
    } else {
        vd_demo(seed);
    }

    render_stop(render_handle);

    return 0;
}
