/*
   vdenc.c - encode files as a Channel VideoDat byte stream

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

// Writes the byte stream as it is carried in the VBI lines, 10 bytes per
// line, to stdout. Pipe it into "videodat -" to send it, or into vdvbi to
// make a simulated VBI capture. It is also the format
// videodat_decoder.py reads with --bytes and writes with --save-bytes.

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "vdpacket.h"

static uint64_t bytes_out = 0;
static vd_stream clock_stream;
static double clock_interval = 5, rate = 1500, next_clock = 0;
static time_t t0;


static void emit(const uint8_t *data, size_t len, void *ctx)
{
    if (fwrite(data, 1, len, stdout) != len) {
        perror("vdenc: write");
        exit(1);
    }
    bytes_out += len;
}


// Times follow the position in the stream, so that a decoded recording
// shows the clock running at the right speed.
static time_t stream_time(void)
{
    return t0 + (time_t)(bytes_out / rate);
}


static time_t stream_now(void *ctx)
{
    return stream_time();
}


static void clock_hook(void *ctx)
{
    double t = bytes_out / rate;
    if (clock_interval > 0 && t >= next_clock) {
        vd_send_clock(&clock_stream, stream_time());
        next_clock += clock_interval;
        if (next_clock < t)
            next_clock = t + clock_interval;
    }
}


static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options] file...\n"
        "  -a stream    data stream number for the files (default 0x03)\n"
        "  -u           send the files on sub channel 1\n"
        "  -r count     transmissions of each group of 4 packets (default 2)\n"
        "  -c seconds   clock packets on stream 01 this often, 0 = off (default 5)\n"
        "  -b rate      bytes per second sent, 250 x lines per frame, used for the\n"
        "               clock and send times (default 1500 = 6 lines)\n"
        "  -i text      send an info block with this name before the files\n"
        "  -t time      transmission start as unix time (default now)\n"
        "  -s seed      seed for the scrambling parameters (default 1)\n"
        "  -l           loop forever\n", prog);
    exit(1);
}


typedef struct file {
    char name[13];
    uint8_t *data;
    uint32_t size;
    time_t mtime;
} file;


static void load(const char *path, file *f)
{
    struct stat st;
    FILE *fp = fopen(path, "rb");
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    if (!fp || fstat(fileno(fp), &st)) {
        fprintf(stderr, "vdenc: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    if (strlen(base) > 12)
        fprintf(stderr, "vdenc: %s: name cut to 12 characters\n", path);
    snprintf(f->name, sizeof f->name, "%s", base);
    for (char *p = f->name; *p; p++)
        *p = toupper((unsigned char)*p);
    f->size = st.st_size;
    f->mtime = st.st_mtime;
    f->data = malloc(f->size ? f->size : 1);
    if (!f->data || fread(f->data, 1, f->size, fp) != f->size) {
        fprintf(stderr, "vdenc: %s: read failed\n", path);
        exit(1);
    }
    fclose(fp);
}


int main(int argc, char *argv[])
{
    int c, repeat = 2, sub = 0, loop = 0;
    unsigned stream = 0x03;
    const char *info = NULL;
    uint32_t seed = 1;

    t0 = time(NULL);

    while ((c = getopt(argc, argv, "a:ur:c:b:i:t:s:l")) != -1) {
        switch (c) {
            case 'a': stream = strtoul(optarg, NULL, 0); break;
            case 'u': sub = 1; break;
            case 'r': repeat = atoi(optarg); break;
            case 'c': clock_interval = strtod(optarg, NULL); break;
            case 'b': rate = strtod(optarg, NULL); break;
            case 'i': info = optarg; break;
            case 't': t0 = strtoll(optarg, NULL, 0); break;
            case 's': seed = strtoul(optarg, NULL, 0); break;
            case 'l': loop = 1; break;
            default: usage(argv[0]);
        }
    }
    if (optind >= argc || stream > 0xfe || repeat < 1 || rate <= 0)
        usage(argv[0]);

    int nfiles = argc - optind;
    file *files = calloc(nfiles, sizeof *files);
    for (int n = 0; n < nfiles; n++)
        load(argv[optind + n], &files[n]);

    vd_tx tx;
    vd_stream fs;
    vd_tx_init(&tx, emit, NULL, seed, repeat);
    tx.hook = clock_hook;
    tx.now = stream_now;
    vd_stream_init(&fs, &tx, stream, sub);
    vd_clock_init(&clock_stream, &tx);

    do {
        if (info)
            vd_send_info(&fs, info, nfiles);
        for (int n = 0; n < nfiles; n++)
            vd_send_file(&fs, files[n].name, files[n].data, files[n].size,
                         files[n].mtime);
    } while (loop);
    vd_flush(&fs);

    // fill up the last line
    static const uint8_t pad[9] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    if (bytes_out % 10)
        emit(pad, 10 - bytes_out % 10, NULL);
    return 0;
}
