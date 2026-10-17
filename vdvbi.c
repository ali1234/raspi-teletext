/*
   vdvbi.c - simulate a VBI capture of a Channel VideoDat byte stream

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

// Reads the byte stream (as written by vdenc) from stdin and writes what
// a bttv card captures from /dev/vbi0 in PAL: 2048 8 bit samples per line
// at 35.46895 MHz, 16 lines per field from line 7 (field 1) and 320
// (field 2), both fields of a frame one after the other. The lines are
// band limited and noise can be added, so that the whole chain down to
// videodat_decoder.py can be tested without a Raspberry Pi and capture
// card.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vdline.h"

#define FS (35.46895e6)
#define SAMPLES (2048)
#define VBI_LINES (16)
#define DEFAULT_MASK (0x0070)

static uint32_t rng = 1;


static double uniform(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (rng + 0.5) / 4294967296.0;
}


static double gauss(void)
{
    return sqrt(-2 * log(uniform())) * cos(2 * M_PI * uniform());
}


static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options] < stream > capture.vbi\n"
        "  -m mask      lines of field 1 with data, bit n = line 7+n (default 0x0070)\n"
        "  -o mask      lines of field 2 with data, bit n = line 320+n (default as -m)\n"
        "  -x us        start of the first bit in the capture line (default 7.0)\n"
        "  -b level     black level (default 60)\n"
        "  -w level     white level (default 200)\n"
        "  -g ns        rise time as gaussian sigma in ns, 0 = sharp (default 60)\n"
        "  -n sigma     gaussian noise in sample units (default 0)\n"
        "  -s seed      seed for the noise (default 1)\n", prog);
    exit(1);
}


int main(int argc, char *argv[])
{
    int c;
    uint16_t mask[2] = { DEFAULT_MASK, DEFAULT_MASK };
    int have_m = 0, have_o = 0;
    double start_us = 7.0, black = 60, white = 200, sigma_ns = 60, noise = 0;

    while ((c = getopt(argc, argv, "m:o:x:b:w:g:n:s:")) != -1) {
        switch (c) {
            case 'm': mask[0] = strtol(optarg, NULL, 0); have_m = 1; break;
            case 'o': mask[1] = strtol(optarg, NULL, 0); have_o = 1; break;
            case 'x': start_us = strtod(optarg, NULL); break;
            case 'b': black = strtod(optarg, NULL); break;
            case 'w': white = strtod(optarg, NULL); break;
            case 'g': sigma_ns = strtod(optarg, NULL); break;
            case 'n': noise = strtod(optarg, NULL); break;
            case 's': rng = strtoul(optarg, NULL, 0) | 1; break;
            default: usage(argv[0]);
        }
    }
    if (have_m && !have_o)
        mask[1] = mask[0];
    if (have_o && !have_m)
        mask[0] = mask[1];

    // gaussian low pass
    double sigma = sigma_ns * 1e-9 * FS;
    int taps = (int)ceil(3 * sigma);
    double kernel[2 * 64 + 1];
    if (taps > 64)
        taps = 64;
    double ksum = 0;
    for (int k = -taps; k <= taps; k++) {
        kernel[k + taps] = sigma > 0 ? exp(-0.5 * k * k / (sigma * sigma)) : 1;
        ksum += kernel[k + taps];
    }

    double spb = FS / VD_BIT_RATE;
    double start = start_us * 1e-6 * FS;
    uint8_t bytes[VD_LINE_BYTES], bits[VD_LINE_BITS];
    double ideal[SAMPLES];
    uint8_t frame[2 * VBI_LINES][SAMPLES];
    int more = 1;

    while (more) {
        int used = 0;
        for (int f = 0; f < 2; f++) {
            for (int n = 0; n < VBI_LINES; n++) {
                int data = 0;
                if (more && (mask[f] >> n & 1)) {
                    size_t got = fread(bytes, 1, VD_LINE_BYTES, stdin);
                    if (got < VD_LINE_BYTES) {
                        more = 0;
                        memset(bytes + got, 0xff, VD_LINE_BYTES - got);
                    }
                    data = got > 0;
                }
                if (data) {
                    vd_line_bits(bytes, bits);
                    used = 1;
                }
                for (int x = 0; x < SAMPLES; x++) {
                    double b = floor((x - start) / spb);
                    int on = data && b >= 0 && b < VD_LINE_BITS && bits[(int)b];
                    ideal[x] = on ? white : black;
                }
                for (int x = 0; x < SAMPLES; x++) {
                    double v = 0;
                    for (int k = -taps; k <= taps; k++) {
                        int i = x + k;
                        i = i < 0 ? 0 : (i >= SAMPLES ? SAMPLES - 1 : i);
                        v += ideal[i] * kernel[k + taps];
                    }
                    v = v / ksum + (noise > 0 ? noise * gauss() : 0);
                    frame[f * VBI_LINES + n][x] = v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)lround(v));
                }
            }
        }
        if (!used && !more)
            break;
        if (fwrite(frame, sizeof frame, 1, stdout) != 1) {
            perror("vdvbi: write");
            return 1;
        }
    }
    return 0;
}
