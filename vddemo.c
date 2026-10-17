/*
   vddemo.c - Channel VideoDat demo transmission

   This file is part of raspi-teletext.

   raspi-teletext is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#include <stdio.h>
#include <time.h>

#include "vdbuffer.h"
#include "vddemo.h"
#include "vdpacket.h"


static void emit(const uint8_t *data, size_t len, void *ctx)
{
    vd_push_bytes(data, len);
}


static vd_stream clock_stream;
static time_t last_clock = 0;

static void clock_hook(void *ctx)
{
    time_t now = time(NULL);
    if (now - last_clock >= 5) {
        vd_send_clock(&clock_stream, now);
        last_clock = now;
    }
}


void vd_demo(uint32_t seed)
{
    static uint8_t text[4096];
    vd_tx tx;
    vd_stream files;
    size_t len = 0;

    vd_tx_init(&tx, emit, NULL, seed, 2);
    tx.hook = clock_hook;
    vd_stream_init(&files, &tx, 0x03, 0);
    vd_clock_init(&clock_stream, &tx);
    for (int n = 1; len < sizeof text - 80; n++)
        len += snprintf((char *)text + len, sizeof text - len,
                        "raspi-teletext VideoDat test, line %d\r\n", n);

    while (1) {
        vd_send_info(&files, "RASPI-VD", 1);
        vd_send_file(&files, "RASPI.TXT", text, len, time(NULL));
    }
}
