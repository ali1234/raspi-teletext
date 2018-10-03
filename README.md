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

Build the programs:

    make

Twiddle the registers:

    sudo ./tvctl on

Ensure you see the message "Teletext output is now on."

If you're connected via PAL, run the demo:

    ./teletext

and press the text button on your TV remote.

If you're connected using NTSC, run the demo:

    ./cea608

and enable the TV's closed caption controls to show CC1.

Detailed Usage
--------------

    tvctl on|off

This tool prepares the composite out for teletext transmission by
shifting the output picture into the VBI area. It will check the
registers are in a known state before doing anything. "on" and
"off" commands will have no effect if the state is already on or
off, or if the registers are in an unknown state.

    teletext [-m even field line mask] [-o odd field line mask] [-]

Optional line mask arguments are a 16 bit mask to create quiet lines
in vbi output, first line is LSB, last is MSB. For example running
"teletext -m 0xFFF0 -o 0x0FFF" will output teletext packets on the first four lines of even fields and last four lines of odd fields. If only one mask is provided the same value will be used for both fields.

Running with no arguments will show a demo. Running "teletext -"
will read packets from stdin and display them. You can therefore
pipe packets from another tool, possibly over the network with ssh
or netcat.

The packet format is 42 byte raw binary packets, without the clock
run-in. Each packet received will be transmitted once, so you must
send packets endlessly. See

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
