Teletext for Raspberry Pi
-------------------------

This software generates a teletext signal in software. No hardware
mods are needed.

This support both PAL (Teletext) and NTSC (CEA608 format captions).

(The demo mode should no longer crash TVs!)

Usage:

Have a Raspberry Pi connected to a TV by composite video.
It doesn't matter if you are running X or not. Dispmanx will draw over
anything else.

At least 32MB of GPU memory is required.

Raspbian defaults to NTSC composite output. To permanently change it
to PAL, put `sdtv=2` in `config.txt` and reboot.

Build the programs:

    make

Twiddle the registers (not needed on KMS systems, see below):

    sudo ./tvctl on

Ensure you see the message "Teletext output is now on."

If you're connected via PAL, run the demo:

    ./teletext

and press the text button on your TV remote.

If you're connected using NTSC, run the demo:

    ./cea608

and enable the TV's closed caption controls to show CC1.

Raspberry Pi 4 and other KMS systems
------------------------------------

Current Raspberry Pi OS and Debian images use the vc4-kms-v3d kernel
driver. Dispmanx is not available there, so the old renderer cannot
run. When `/opt/vc/include/bcm_host.h` is
missing, `make` builds the programs against DRM/KMS instead (force this
with `make RENDER=kms`, or the old renderer with `make RENDER=dispmanx`).
This needs the libdrm headers:

    sudo apt install libdrm-dev pkg-config

Enable composite output in `config.txt` (`/boot/firmware/config.txt`
on Bookworm) and reboot. On the Pi 4 this turns HDMI off:

    dtoverlay=vc4-kms-v3d,composite
    enable_tvout=1

There is no need to run tvctl. The programs set a video mode with extra
lines at the top of each field, which programs the same timing tvctl
does, and select PAL (teletext) or NTSC (cea608) themselves. They must
be the only program using the display, so run them as root from a text
console, not from inside a desktop session. The console stays visible
below the VBI lines, and the normal mode comes back when the program
exits.

If the raspi-teletext gets interrupted at startup check if a desktop manager is running. It has to be stopped before the program can run, i.e.

```sudo systemctl stop lightdm```

At startup the programs measure the field lengths to find out which
field is which, and print the result. If teletext pages come out
garbled, try again with `RASPI_TELETEXT_SWAP_FIELDS=1` set in the
environment, and please report which one works.

The data is moved 20 pixels left of where dispmanx put it, which puts
the clock run-in where real broadcasts have it on a Pi 4. Set
`RASPI_TELETEXT_XSHIFT` to a number of pixels (74 ns each) to adjust
it.

Detailed Usage
--------------

    tvctl on|off

This tool prepares the composite out for teletext transmission by
shifting the output picture into the VBI area. It will check the
registers are in a known state before doing anything. "on" and
"off" commands will have no effect if the state is already on or
off, or if the registers are in an unknown state.

    teletext [-m first field mask] [-o second field mask] \
             [-l white level] [-f] [-]

Without arguments, teletext shows a demo: a page with the Pi's host
name, IP address, CPU frequency and load, memory and temperature, and
the date and time in the page header. With "-" as
the last argument, it sends packets read from stdin instead (see
"Sending your own packets" below).

### Line masks (`-m` and `-o`)

By default teletext uses all 16 VBI lines of each field: lines 7-22 on
the first field and 320-335 on the second, 32 lines per frame. The line
masks switch lines off. They are 16 bit numbers, best written in
hexadecimal with a leading `0x` (without it, the number is read as
decimal). Bit n set means line 7+n on the first field (`-m`) or line
320+n on the second field (`-o`) stays quiet (black); bit n clear means
it carries teletext. This is the other way round from `videodat`. If
only one of `-m` and `-o` is given, it is used for both fields.

| bit | 0  | 1  | 2  | 3  | 4  | 5  | 6  | 7  | 8  | 9  | 10 | 11 | 12 | 13 | 14 | 15 |
|-----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `-m` line | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 |
| `-o` line | 320 | 321 | 322 | 323 | 324 | 325 | 326 | 327 | 328 | 329 | 330 | 331 | 332 | 333 | 334 | 335 |

Each line carries one packet per frame, so every line in the frame adds
25 packets per second. Examples, with the same mask on both fields:

| mask   | teletext on first field | teletext on second field | lines per frame | packets/s |
|--------|-------------------------|--------------------------|-----------------|-----------|
| 0x0000 | 7-22                    | 320-335                  | 32 (default)    | 800       |
| 0x0070 | 7-10, 14-22             | 320-323, 327-335         | 26              | 650       |
| 0xFF00 | 7-14                    | 320-327                  | 16              | 400       |
| 0x00FF | 15-22                   | 328-335                  | 16              | 400       |
| 0xFFF0 | 7-10                    | 320-323                  | 8               | 200       |
| 0x0FFF | 19-22                   | 332-335                  | 8               | 200       |
| 0xFFFE | 7                       | 320                      | 2               | 50        |
| 0xFFFF | none                    | none                     | 0               | 0         |

0x0070 keeps lines 11-13 and 324-326 free, the lines Channel VideoDat
used. Different masks per field are added up: `-m 0xFFF0 -o 0x0FFF`
sends on lines 7-10 of the first field and 332-335 of the second, 8
lines per frame, 200 packets/s.

The first field is the one with lines 1-312. On KMS systems the order
of the fields is measured at startup; if it comes out wrong (see
`RASPI_TELETEXT_SWAP_FIELDS` above), the two masks swap too.

### Other options

`-l` sets the white level, the brightness of the 1 bits, from 0 to 100
(default 100). Lower it if the signal overshoots on your TV or capture
card.

`-f` turns on full-field mode: every line of the picture carries
teletext, not just the VBI lines, for a much higher data rate (576
lines per frame with dispmanx, 608 with KMS, so 14400 or 15200
packets/s). The masks still apply to the first 16 lines of each field;
all lines below them always carry data. Most TVs and decoders only look
at the VBI lines and can't use this mode.

### Sending your own packets

"teletext -" reads packets from stdin. A packet is 42 bytes of raw
binary data, exactly as it is sent on the line after the clock run-in
and framing code:

| bytes | content |
|-------|---------|
| 0-1   | magazine and packet number (MRAG), Hamming 8/4 coded |
| 2-41  | 40 bytes of packet data, eg a page header or a row of text with odd parity |

This is the same format as `.t42` files. teletext puts the clock run-in
and framing code (55 55 27) in front of each packet itself and sends the
bytes as they are, LSB first; it doesn't add Hamming codes or parity, so
the packets must already have them.

The packets are sent in the order they arrive, one per line, from the
top line of the first field to the bottom line of the second. Each
packet is sent once. A TV only shows a page that arrives after it was
asked for, so pages have to be sent over and over, as a carousel, the
way a broadcaster does. teletext buffers 64 packets; when the buffer is
full it stops reading until there is room, and when it runs empty it
sends filler packets (a blank row 25 of magazine 8) until more arrive.
When stdin ends, teletext exits.

Packets can come from any program, and over the network with ssh or
netcat, for example:

    while true; do cat pages.t42; done | sudo ./teletext -

    # on the Pi
    nc -l -p 5000 | sudo ./teletext -
    # on another machine
    some-teletext-generator | nc raspberrypi 5000

[vbit2](https://github.com/peterkvt80/vbit2) makes a teletext service
from page files and writes it in this format.

See

http://www.etsi.org/deliver/etsi_i_ets/300700_300799/300706/01_60/ets_300706e01p.pdf

for details of the teletext protocol.

    cea608 <->

Running with no arguments will show a demo.  Running "cea608 -"
will read data from stdin and display.  The data is in binary format,
with each field represented as two bytes of parity-encoded data.
This is the "raw" format some capture tools can output.  Data is
output at 59.97 fields per second.  See

https://en.wikipedia.org/wiki/EIA-608

for details on the protocol and links to the specifications.

Channel VideoDat
----------------

`videodat` sends data the way Channel VideoDat did, a service Pro7 ran
in the VBI from 1990 to 1994. Together with the decoder in
https://github.com/itewreed/channel-videodat-decoder this allows the
whole chain from files to a VBI capture and back to be tested.

    videodat [-m first field lines] [-o second field lines] [-l level] \
             [-x start] [-s seed] [-]

With no arguments, a small text file and the clock are sent over and
over. Running "videodat -" sends the byte stream read from stdin, which
`vdenc` makes from files:

    ./vdenc -l -i MYPACKET FILE1.EXE README.TXT | sudo ./videodat -

The line masks select the lines carrying data. They are 16 bit numbers,
best written in hexadecimal with a leading `0x` (without it, the number
is read as decimal). Bit n set means line 7+n on the first field (`-m`)
or line 320+n on the second field (`-o`) carries data; note that this is
the other way round from teletext. If only one of `-m` and `-o` is
given, it is used for both fields. The default, 0x0070, is lines 11-13
and 324-326, which is where Pro7 sent it.

| bit | 0  | 1  | 2  | 3  | 4  | 5  | 6  | 7  | 8  | 9  | 10 | 11 | 12 | 13 | 14 | 15 |
|-----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| `-m` line | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 |
| `-o` line | 320 | 321 | 322 | 323 | 324 | 325 | 326 | 327 | 328 | 329 | 330 | 331 | 332 | 333 | 334 | 335 |

Examples, with the same mask on both fields:

| mask   | first field | second field | lines per frame | bytes/s | `vdenc -b` |
|--------|-------------|--------------|-----------------|---------|------------|
| 0x0010 | 11          | 324          | 2               | 500     | 500        |
| 0x0070 | 11-13       | 324-326      | 6 (default)     | 1500    | 1500       |
| 0x00F0 | 11-14       | 324-327      | 8               | 2000    | 2000       |
| 0x01F0 | 11-15       | 324-328      | 10              | 2500    | 2500       |
| 0x0FF0 | 11-18       | 324-331      | 16              | 4000    | 4000       |
| 0x00FF | 7-14        | 320-327      | 16              | 4000    | 4000       |
| 0xFF00 | 15-22       | 328-335      | 16              | 4000    | 4000       |
| 0xFFFF | 7-22        | 320-335      | 32              | 8000    | 8000       |

Different masks per field are added up: `-m 0x0070 -o 0x0010` is lines
11-13 and 324, 4 lines per frame, 1000 bytes/s. Each line carries 10
bytes, 25 times a second, so the data rate is 250 bytes/s per line in
the frame.

`-x` moves the first bit, in pixels of 74 ns (default 44, matched to a
recording), and `-s` seeds the scrambling. The other options are as for
teletext.

    vdenc [-a stream] [-u] [-r count] [-c seconds] [-b rate] [-i text] \
          [-t time] [-s seed] [-l] file...

writes the files as packets to stdout: a header packet, one packet per
255 bytes and an end packet per file, with clock packets in between.
`-r` sets how often each group of 4 packets is sent (2, as broadcast),
`-l` loops forever.

vdenc doesn't know how fast its output is sent, so `-b` has to tell it:
the clock packets (every `-c` seconds) and the send times in the file
headers are worked out from the number of bytes written and `-b`. Set
it to 250 bytes/s times the number of lines per frame `videodat` (or
`vdvbi`) uses, as in the table above. The default, 1500, fits the
default 6 lines. With more lines and the default `-b`, the clock in the
stream runs slow; with 12 lines, for example, it runs at half speed.
For example, for 10 lines:

    ./vdenc -l -b 2500 FILE.ZIP | sudo ./videodat -m 0x01F0 -

`videodat` without `-` (the demo) takes its times from the system clock
and doesn't need this. vdenc doesn't need a Raspberry Pi, and its output
can be read by `videodat_decoder.py --bytes`.

    vdvbi [-m mask] [-o mask] [-x us] [-b black] [-w white] [-g ns] \
          [-n noise] [-s seed] < stream > capture.vbi

turns the byte stream into what a bttv card captures from /dev/vbi0
(2048 samples per line, 32 lines per frame), with a band limit and
optional noise, so error correction can be tried out without hardware:

    ./vdenc FILE.ZIP | ./vdvbi -n 15 > test.vbi
    python3 videodat_decoder.py test.vbi

The signal: 10 bytes per line, NRZ at 2 Mbit/s starting about 11.7 us
after the line sync. Each byte is an inverted UART frame: a white start
bit, 8 data bits LSB first with 1 = black, and a black stop bit. The
packets (sync 56 AE, header, scrambled payload, 16 bit sum) are
described in vdpacket.c.

These details were checked against seven recordings from 1992 to 1994:
the group counter in bits 4..6 of B is ((C+1)>>2)&7 on file streams
(groups end at C = 2, 6, 10, ... and run on across files) and (C>>2)&7
on stream 01, whose packets are sent only once; the send time in file
headers, end packets and info blocks is the DOS date and time the packet
goes out; info blocks are 00, send time, a 16 bit count, the name length
and the name. Still unknown, and sent as zeros: the last two bytes of
the end packet and what follows the name in an info block.

Teletext and VideoDat together (experimental)
---------------------------------------------

**This program is experimental. It has only been checked in software,
not on a TV or capture card, and teletext may not decode reliably.**

`teletext` and `videodat` can't run at the same time: each needs the
display to itself (KMS), or would cover the other's lines (dispmanx).
`ttvd` sends both in one picture, Pro7 style, with teletext on some VBI
lines and VideoDat on others:

    ttvd [-t file] [-v file] [-m mask] [-o mask] [-M mask] [-O mask] \
         [-l level] [-x start] [-s seed] [-w width]

`-t` is the teletext input (42 byte packets, as for "teletext -") and
`-v` the VideoDat input (the byte stream from `vdenc`, as for
"videodat -"). "-" reads stdin, which only one of them can do. Without
`-t` or `-v`, that part shows its demo. For example:

    ./vdenc -l FILE.ZIP | sudo ./ttvd -t pages.t42 -v -

The masks work as in the two programs, in hexadecimal: `-m` and `-o`
are the teletext masks (bit set = no teletext on the line), `-M` and
`-O` the VideoDat masks (bit set = VideoDat on the line). Bit n is line
7+n on the first field and 320+n on the second, see the tables above.
All four default to 0x0070, so VideoDat is on lines 11-13 and 324-326
and teletext on the other 26 lines (650 packets/s). A line in both
masks carries VideoDat, and ttvd warns about it. For example, teletext
only on lines 7-10 and 320-323 next to the default VideoDat lines:

    sudo ./ttvd -m 0xFFF0 -t pages.t42

`-l`, `-x` and `-s` are as for videodat. ttvd runs until both inputs
have ended; with a demo, it runs until it is stopped.

Why this is experimental: teletext draws one pixel per teletext bit,
370 pixels for the whole line, while videodat needs 720 pixels (13.5
MHz) for its 2 Mbit/s signal. Both have to share one image here, which
the display hardware scales to the line. `-w` sets the width of that
image. The default, 1480, gives exactly 4 pixels per teletext bit; the
VideoDat bits are then 13.9 pixels wide, so their edges can be up to 18
ns (4% of a VideoDat bit) off.

Captures with a bt848 card at several widths, compared with `teletext`
alone (edge timing measured on every 10th frame, teletext and VideoDat
decoded from all 30 seconds):

| capture         | teletext edge jitter | worst edge | VideoDat edge jitter |
|-----------------|----------------------|------------|----------------------|
| teletext only   | 1.9 ns rms           | 11 ns      | -                    |
| ttvd -w 720     | 21 ns rms            | 44 ns      | 23 ns rms            |
| ttvd -w 1440    | 10.5 ns rms          | 23 ns      | 10 ns rms            |
| ttvd -w 1480    | 3.1 ns rms           | 9 ns       | 11 ns rms            |

A teletext bit is 144 ns. At 720 pixels a teletext bit is 1.946 pixels
wide and its edges land up to half a pixel off, which made teletext
less reliable on a TV. At 1440 pixels the steps are half as big. At
1480 every teletext bit is 4 pixels wide, and as the scaler keeps edge
positions between output pixels, the edges are almost as clean as with
`teletext` alone. All VideoDat packets had good checksums at every
width.
