/*
   ttvd.c - teletext and Channel VideoDat at the same time (experimental)

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

// teletext and videodat each draw their own image at their own pixel
// rate: one pixel per teletext bit (370 pixels per line), or 720 pixels
// per line at 13.5 MHz for VideoDat. Here both go into one image, which
// the display hardware scales to the line. The scaler interpolates, so
// edges keep their position between output pixels. The default width of
// 1480 gives exactly 4 pixels per teletext bit, so its edges are where
// they belong; the 500 ns VideoDat bits are 13.9 pixels wide and their
// edges up to 18 ns off. Captures showed teletext edge jitter of 21 ns
// rms at 720 pixels, 11 ns at 1440 and 3 ns at 1480, against 2 ns from
// teletext alone.

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "render.h"
#include "buffer.h"
#include "demo.h"
#include "vdbuffer.h"
#include "vddemo.h"
#include "vdline.h"

#define VBI_LINES (16)
#define HEIGHT (2*VBI_LINES)

// Teletext as teletext.c draws it: 370 pixels per line, one per bit, with
// the clock run-in starting at pixel 8.
#define TT_WIDTH (370)
#define TT_OFFSET (8)
#define TT_BITS (24 + 42*8)
#define TT_CLOCK (0x275555)

// Bit n of a mask is line 7+n (first field) or 320+n (second field).
#define DEFAULT_MASK (0x0070)

int width = 4 * TT_WIDTH;    // 4 pixels per teletext bit
uint16_t tt_mask[2];        // bit set = no teletext on this line
uint16_t vd_mask[2];        // bit set = VideoDat on this line
double vd_start = 44;       // first VideoDat bit, in pixels of 74 ns


static void tt_line_pixels(uint8_t *pixels)
{
    uint8_t bits[TT_BITS];
    uint8_t *p = bits;
    for (int n = 0, c = TT_CLOCK; n < 24; n++, c >>= 1)
        *p++ = c & 1;
    get_packet(p);          // 42 bytes, one bit per byte, LSB first

    // As in vd_line_pixels, each pixel shows the bit at its centre.
    double ppb = (double)width / TT_WIDTH;
    for (int x = 0; x < width; x++) {
        int b = (int)((x + 0.5) / ppb) - TT_OFFSET;
        pixels[x] = (b >= 0 && b < TT_BITS) ? bits[b] : 0;
    }
}


void draw(uint8_t *image, int field)
{
    uint8_t bytes[VD_LINE_BYTES];
    int tt = tt_mask[field], vd = vd_mask[field];
    double scale = width / 720.0;
    for (int n = 0; n < VBI_LINES; n++) {
        uint8_t *row = image + PITCH(width) * (2*n+field);
        if (vd & 1) {
            // VideoDat has the line even when it has nothing to send
            if (vd_get_line(bytes))
                vd_line_pixels(bytes, row, width, vd_start * scale,
                               scale * 13.5e6 / VD_BIT_RATE);
            else
                memset(row, 0, width);
        } else if (!(tt & 1)) {
            tt_line_pixels(row);
        } else {
            memset(row, 0, width);
        }
        tt >>= 1;
        vd >>= 1;
    }
}


void init(uint8_t *image)
{
    draw(image, 0);
}


static FILE *tt_in = NULL, *vd_in = NULL;
static uint32_t seed;


static void *tt_thread(void *arg)
{
    if (!tt_in) {
        demo();
        return NULL;
    }
    while (read_packets_from(tt_in))
        ;
    wait_packets_sent();
    return NULL;
}


static void *vd_thread(void *arg)
{
    if (!vd_in) {
        vd_demo(seed);
        return NULL;
    }
    while (vd_read_lines_from(vd_in))
        ;
    vd_push_flush();
    vd_wait_empty();
    return NULL;
}


static FILE *open_input(const char *name)
{
    if (strcmp(name, "-") == 0)
        return stdin;
    FILE *f = fopen(name, "rb");
    if (!f) {
        perror(name);
        exit(1);
    }
    return f;
}


static void masks(uint16_t *mask, const char *m, const char *o)
{
    if (m)
        mask[0] = mask[1] = strtol(m, NULL, 0);
    if (o) {
        mask[1] = strtol(o, NULL, 0);
        if (!m)
            mask[0] = mask[1];
    }
}


static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -t file   teletext packets (42 bytes each), - = stdin (default demo)\n"
        "  -v file   VideoDat byte stream from vdenc, - = stdin (default demo)\n"
        "  -m mask   teletext: quiet lines of the first field (default 0x0070)\n"
        "  -o mask   teletext: quiet lines of the second field (default as -m)\n"
        "  -M mask   VideoDat: lines of the first field (default 0x0070)\n"
        "  -O mask   VideoDat: lines of the second field (default as -M)\n"
        "  -l level  white level 0-100 (default 100)\n"
        "  -x start  first VideoDat bit in pixels of 74 ns (default 44)\n"
        "  -s seed   seed for the VideoDat demo scrambling\n"
        "  -w width  image width in pixels for the whole line (default 1480)\n",
        prog);
    exit(1);
}


int main(int argc, char *argv[])
{
    int c, level = 100;
    char *m = NULL, *o = NULL, *M = NULL, *O = NULL;
    char *tt_name = NULL, *vd_name = NULL;
    seed = time(NULL);

    while ((c = getopt(argc, argv, "t:v:m:o:M:O:l:x:s:w:")) != -1) {
        switch (c) {
            case 't': tt_name = optarg; break;
            case 'v': vd_name = optarg; break;
            case 'm': m = optarg; break;
            case 'o': o = optarg; break;
            case 'M': M = optarg; break;
            case 'O': O = optarg; break;
            case 'l':
                level = strtol(optarg, NULL, 0);
                if (level < 0) level = 0;
                if (level > 100) level = 100;
                break;
            case 'x': vd_start = strtod(optarg, NULL); break;
            case 's': seed = strtoul(optarg, NULL, 0); break;
            case 'w': width = strtol(optarg, NULL, 0); break;
            default: usage(argv[0]);
        }
    }
    if (optind < argc || width < TT_WIDTH || width > 4096)
        usage(argv[0]);
    if (tt_name && vd_name && !strcmp(tt_name, "-") && !strcmp(vd_name, "-")) {
        fprintf(stderr, "%s: only one of -t and -v can read stdin\n", argv[0]);
        return 1;
    }

    tt_mask[0] = tt_mask[1] = DEFAULT_MASK;
    vd_mask[0] = vd_mask[1] = DEFAULT_MASK;
    masks(tt_mask, m, o);
    masks(vd_mask, M, O);
    for (int f = 0; f < 2; f++)
        if (~tt_mask[f] & vd_mask[f] & 0xffff)
            fprintf(stderr, "Warning: lines 0x%04x of field %d are in both masks, "
                    "they carry VideoDat.\n", ~tt_mask[f] & vd_mask[f] & 0xffff, f + 1);

    if (tt_name)
        tt_in = open_input(tt_name);
    if (vd_name)
        vd_in = open_input(vd_name);

    void *render_handle = render_start(width, HEIGHT, 0, 0, init, draw, -1, level, VBI_LINES, "PAL");

    // Runs until both inputs have ended; a demo never ends.
    pthread_t tt, vd;
    pthread_create(&tt, NULL, tt_thread, NULL);
    pthread_create(&vd, NULL, vd_thread, NULL);
    pthread_join(tt, NULL);
    pthread_join(vd, NULL);
    usleep(100000); // let the last lines go out

    render_stop(render_handle);
    return 0;
}
