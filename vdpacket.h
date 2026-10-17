/*
   vdpacket.h - Channel VideoDat packet encoder

   This file is part of raspi-teletext.

   raspi-teletext is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#ifndef VDPACKET_H
#define VDPACKET_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define VD_SYNC0 (0x56)
#define VD_SYNC1 (0xAE)
#define VD_KEY_XOR (0x58)

// Largest payload sent by this encoder (2^8). All packets seen in
// recordings are this size or smaller.
#define VD_MAX_LOG2 (8)
#define VD_MAX_PAYLOAD (1 << VD_MAX_LOG2)
// 9 header bytes, payload, 2 checksum bytes, plus room for the 0xFF
// escape bytes inserted after every 56 AE inside a packet.
#define VD_MAX_PACKET ((VD_MAX_PAYLOAD + 11) * 3 / 2)

// Packets are sent in groups of this many, and each group is repeated.
#define VD_GROUP (4)

// Payload types (first payload byte) used for file transfer.
#define VD_TYPE_INFO   (0x00)
#define VD_TYPE_HEADER (0x02)
#define VD_TYPE_END    (0x03)
#define VD_TYPE_DATA   (0x04)
#define VD_TYPE_CLOCK  (0x06)

// Data block size of file transfer (payload size minus the type byte).
#define VD_BLOCK (VD_MAX_PAYLOAD - 1)

typedef void (*vd_emit_func)(const uint8_t *data, size_t len, void *ctx);

typedef struct vd_tx {
    vd_emit_func emit;
    void *ctx;
    uint32_t rng;       // state of the generator for the scrambling parameters
    int repeat;         // how often each group of packets is sent
    // Called before each group is sent, eg to slip in a clock packet.
    void (*hook)(void *ctx);
    int in_hook;
    // Time put into file headers, end packets and info blocks. NULL
    // means the current time.
    time_t (*now)(void *ctx);
} vd_tx;

typedef struct vd_stream {
    vd_tx *tx;
    uint8_t A;          // data stream number
    uint8_t sub;        // sub channel (bit 7 of B)
    uint8_t C;          // packet counter of the next packet
    // Groups are counted in bits 4..6 of B as ((C + phase) >> 2) & 7.
    // Recordings have phase 1 on file streams, so the first group of a
    // stream is C 0..2, and phase 0 on stream 01.
    int phase;
    int repeat;         // how often each group is sent
    int queued;
    uint8_t payload[VD_GROUP][VD_MAX_PAYLOAD];
    uint8_t log2[VD_GROUP];
} vd_stream;

// Build one packet. payload holds 2^(B & 15) bytes of clear data. Writes
// sync, header, scrambled payload and checksum to out, with 0xFF inserted
// after each 56 AE following the sync word, and returns the length.
size_t vd_build_packet(uint8_t *out, uint8_t A, uint8_t B, uint8_t C, const uint8_t *payload,
                       uint8_t D, uint8_t E, uint8_t F, uint8_t S);

void vd_tx_init(vd_tx *tx, vd_emit_func emit, void *ctx, uint32_t seed, int repeat);
void vd_stream_init(vd_stream *s, vd_tx *tx, uint8_t A, int sub);

// Queue a payload of up to VD_MAX_PAYLOAD bytes. It is padded with zeros
// to the next power of two. A full group is sent at once.
int vd_send(vd_stream *s, const uint8_t *payload, size_t len);
// Send the queued packets as a (short) group.
void vd_flush(vd_stream *s);

// MS-DOS date and time (date in the high word), as used in file headers.
uint32_t vd_dos_time(time_t t);

int vd_send_file(vd_stream *s, const char *name, const uint8_t *data, uint32_t size,
                 time_t mtime);
// Clock on stream 01, sent once (not repeated) and at once.
void vd_clock_init(vd_stream *s, vd_tx *tx);
void vd_send_clock(vd_stream *s, time_t now);
void vd_send_info(vd_stream *s, const char *text, int count);

#endif
