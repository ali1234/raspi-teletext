/*
   vdpacket.c - Channel VideoDat packet encoder

   This file is part of raspi-teletext.

   raspi-teletext is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

// Packet format of Channel VideoDat (Pro7, 1990-1994), the inverse of
// videodat_decoder.py from channel-videodat-decoder:
//
//   off  content
//   0    56 AE      sync
//   2    A          data stream (eg 01 clock, 03 software)
//   3    B          bits 0..3: n, payload is 2^n bytes
//                   bits 4..6: group counter, bit 7: sub channel
//   4    C          packet counter, per data stream
//   5    D          bits 0..2: d    d, e and f select the permutation
//   6    E          bit 2: e        of the bytes in each 8 byte block
//   7    F          bits 1..2: f
//   8    S          key k = S xor 0x58
//   9    2^n bytes  scrambled payload
//   9+2^n           checksum (A + B + C + sum(clear payload)) & 0xFFFF, LE
//
// Inside a packet, every 56 AE is followed by an extra FF, so that it
// can't be mistaken for a sync word.

#include <string.h>

#include "vdpacket.h"


static void qperm(uint8_t D, uint8_t E, uint8_t F, int q[8])
{
    int e = (E >> 2) & 1;
    int d = D & 7;
    int f = (F >> 1) & 3;
    int m = (2 * f + 1 + 6 * e) % 8;
    for (int c = 0; c < 8; c++)
        q[c] = (((c ^ e) + (d ^ e) + 1) * m) % 8;
}


size_t vd_build_packet(uint8_t *out, uint8_t A, uint8_t B, uint8_t C, const uint8_t *payload,
                       uint8_t D, uint8_t E, uint8_t F, uint8_t S)
{
    uint8_t raw[VD_MAX_PAYLOAD + 11];
    int n = 1 << (B & 15);
    uint8_t k = S ^ VD_KEY_XOR;
    unsigned sum = A + B + C;
    size_t len = 0;

    raw[0] = VD_SYNC0;
    raw[1] = VD_SYNC1;
    raw[2] = A;
    raw[3] = B;
    raw[4] = C;
    raw[5] = D;
    raw[6] = E;
    raw[7] = F;
    raw[8] = S;
    if (n < 8) {
        // very short packets are not permuted
        for (int i = 0; i < n; i++)
            raw[9 + i] = payload[i] ^ k;
    } else {
        // clear[8g+c] = raw[9 + 8g + (q[c]-1) mod 8] xor k
        int q[8];
        qperm(D, E, F, q);
        for (int i = 0; i < n; i++)
            raw[9 + 8 * (i / 8) + (q[i % 8] + 7) % 8] = payload[i] ^ k;
    }
    for (int i = 0; i < n; i++)
        sum += payload[i];
    raw[9 + n] = sum & 0xff;
    raw[10 + n] = (sum >> 8) & 0xff;

    for (int i = 0; i < n + 11; i++) {
        out[len++] = raw[i];
        if (i >= 3 && raw[i - 1] == VD_SYNC0 && raw[i] == VD_SYNC1)
            out[len++] = 0xff;
    }
    return len;
}


// xorshift32
static uint8_t rnd(vd_tx *tx)
{
    uint32_t x = tx->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    tx->rng = x;
    return x >> 24;
}


void vd_tx_init(vd_tx *tx, vd_emit_func emit, void *ctx, uint32_t seed, int repeat)
{
    tx->emit = emit;
    tx->ctx = ctx;
    tx->rng = seed ? seed : 0x56ae;
    tx->repeat = repeat > 0 ? repeat : 1;
    tx->hook = NULL;
    tx->in_hook = 0;
    tx->now = NULL;
}


static time_t now(vd_tx *tx)
{
    return tx->now ? tx->now(tx->ctx) : time(NULL);
}


void vd_stream_init(vd_stream *s, vd_tx *tx, uint8_t A, int sub)
{
    memset(s, 0, sizeof *s);
    s->tx = tx;
    s->A = A;
    s->sub = sub ? 1 : 0;
    s->phase = 1;
    s->repeat = tx->repeat;
}


void vd_clock_init(vd_stream *s, vd_tx *tx)
{
    vd_stream_init(s, tx, 0x01, 0);
    s->phase = 0;
    s->repeat = 1;
}


void vd_flush(vd_stream *s)
{
    uint8_t pkt[VD_GROUP][VD_MAX_PACKET];
    size_t len[VD_GROUP];

    if (!s->queued)
        return;
    if (s->tx->hook && !s->tx->in_hook) {
        s->tx->in_hook = 1;
        s->tx->hook(s->tx->ctx);
        s->tx->in_hook = 0;
    }
    // All copies of a packet are identical, including the scrambling.
    for (int i = 0; i < s->queued; i++) {
        uint8_t C = s->C - s->queued + i;
        uint8_t B = s->log2[i] | ((C + s->phase) >> 2 & 7) << 4 | s->sub << 7;
        uint8_t D = rnd(s->tx), E = rnd(s->tx), F = rnd(s->tx), S = rnd(s->tx);
        len[i] = vd_build_packet(pkt[i], s->A, B, C, s->payload[i], D, E, F, S);
    }
    for (int r = 0; r < s->repeat; r++)
        for (int i = 0; i < s->queued; i++)
            s->tx->emit(pkt[i], len[i], s->tx->ctx);
    s->queued = 0;
}


int vd_send(vd_stream *s, const uint8_t *payload, size_t len)
{
    int log2 = 0;
    if (len > VD_MAX_PAYLOAD)
        return -1;
    while ((1u << log2) < len)
        log2++;
    memset(s->payload[s->queued], 0, VD_MAX_PAYLOAD);
    memcpy(s->payload[s->queued], payload, len);
    s->log2[s->queued] = log2;
    s->queued++;
    s->C++;
    // A group ends where the group counter changes, also within a file.
    if (((s->C + s->phase) & 3) == 0)
        vd_flush(s);
    return 0;
}


uint32_t vd_dos_time(time_t t)
{
    struct tm tm;
    localtime_r(&t, &tm);
    if (tm.tm_year < 80)
        return 0x00210000; // 1980-01-01
    uint32_t date = (tm.tm_year - 80) << 9 | (tm.tm_mon + 1) << 5 | tm.tm_mday;
    uint32_t tod = tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec / 2;
    return date << 16 | tod;
}


static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}


// File transfer:
//   02 [send time 4] [size LE32] [DOS time 4] [name length] [name]
//   04 [up to 255 data bytes]     one packet per block, counter in C
//   03 [send time 4] 00 [2 bytes] end of file
// The send times are DOS date and time of when the packet goes out; in
// recordings they match the clock packets. The last two bytes of the end
// packet are unknown (they look like a check value) and are sent as 0.
int vd_send_file(vd_stream *s, const char *name, const uint8_t *data, uint32_t size,
                 time_t mtime)
{
    uint8_t p[VD_MAX_PAYLOAD];
    size_t nl = strlen(name);

    if (nl < 1 || nl > 12)
        return -1;
    p[0] = VD_TYPE_HEADER;
    put32(p + 1, vd_dos_time(now(s->tx)));
    put32(p + 5, size);
    put32(p + 9, vd_dos_time(mtime));
    p[13] = nl;
    memcpy(p + 14, name, nl);
    vd_send(s, p, 14 + nl);

    for (uint32_t off = 0; off < size; off += VD_BLOCK) {
        uint32_t n = size - off < VD_BLOCK ? size - off : VD_BLOCK;
        p[0] = VD_TYPE_DATA;
        memcpy(p + 1, data + off, n);
        // The last block goes in the smallest packet it fits.
        vd_send(s, p, 1 + n);
    }

    memset(p, 0, 8);
    p[0] = VD_TYPE_END;
    put32(p + 1, vd_dos_time(now(s->tx)));
    vd_send(s, p, 8);
    return 0;
}


// Clock, every 5 seconds on stream 01: 06 YY MM DD hh mm ss 00
void vd_send_clock(vd_stream *s, time_t now)
{
    struct tm tm;
    uint8_t p[8] = { VD_TYPE_CLOCK };
    localtime_r(&now, &tm);
    p[1] = tm.tm_year;
    p[2] = tm.tm_mon + 1;
    p[3] = tm.tm_mday;
    p[4] = tm.tm_hour;
    p[5] = tm.tm_min;
    p[6] = tm.tm_sec;
    vd_send(s, p, sizeof p);
    vd_flush(s);
}


// Info block at the start of a transmission, eg the name of a software
// package: 00 [send time 4] [count LE16] [length] [name] 00 00 00 ...
// In recordings the count is small (1..37) and 2..4 for the VORSCHAU
// files; it is taken to be the number of files. What follows the name
// varies between years and is sent as zeros.
void vd_send_info(vd_stream *s, const char *text, int count)
{
    uint8_t p[32] = { VD_TYPE_INFO };
    size_t n = strlen(text);
    if (n > sizeof p - 11)
        n = sizeof p - 11;
    put32(p + 1, vd_dos_time(now(s->tx)));
    p[5] = count;
    p[6] = count >> 8;
    p[7] = n;
    memcpy(p + 8, text, n);
    vd_send(s, p, sizeof p);
}
