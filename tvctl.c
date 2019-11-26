/*
 * devmem3.c: Simple program to dump memory range.
 *
 *  Copyright (C) 2000, Jan-Derk Bakker (jdb@lartmaker.nl)
 *  Copyright (C) 2008, Alistair Buxton (a.j.buxton@gmail.com)
 *
 * This software has been developed for the LART computing board
 * (http://www.lart.tudelft.nl/). The development has been sponsored by
 * the Mobile MultiMedia Communications (http://www.mmc.tudelft.nl/)
 * and Ubiquitous Communications (http://www.ubicom.tudelft.nl/)
 * projects.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/mman.h>

#define MAP_SIZE 4096UL
#define MAP_MASK (MAP_SIZE - 1)

typedef enum {PAL_OFF, PAL_ON, NTSC_OFF, NTSC_ON, UNKNOWN} tstate;

int try_set_regs(volatile unsigned int *regs, int argc, char *argv[])
{
    // each register is two shorts

    // eg: reg[5] = 0014 0003 - means 0x14 lines then 0x3 lines above the visible
    //     reg[6] = 0002 0120 - means 0x120 visible lines, then 0x2 lines below the display
    //     add them all you get 313 for odd field and 312 for even field, for 625 total

    // therefore, reducing the two values in reg[5] and increasing the first value in reg[6]
    // will shift the visible lines up.

    // question: what is the difference between the two values in reg[5]/reg[7] register?

    tstate state;

    if (regs[5] == 0x00140003 && regs[6] == 0x00020120 && regs[7] == 0x00130003 && regs[8] == 0x00020120)
        state = PAL_OFF;
    else if (regs[5] == 0x00040003 && regs[6] == 0x00120120 && regs[7] == 0x00030003 && regs[8] == 0x00120120)
        state = PAL_ON;
    else if (regs[5] == 0x00100003 && regs[6] == 0x000300f0 && regs[7] == 0x00100003 && regs[8] == 0x000400f0)
        state = NTSC_OFF;
    else if (regs[5] == 0x000e0003 && regs[6] == 0x000500f0 && regs[7] == 0x000e0003 && regs[8] == 0x000600f0)
        state = NTSC_ON;
    else
        state = UNKNOWN;

    if (argc == 2 && argv[1][1] == 'n') {
        switch(state) {
            case PAL_OFF:
                regs[5] = 0x00040003;
                regs[6] = 0x00120120;
                regs[7] = 0x00030003;
                regs[8] = 0x00120120;
                /* fallthrough */
            case PAL_ON:
                fprintf(stderr, "Teletext output is now on.\n");
                return 1;
            case NTSC_OFF:
                regs[5] = 0x000e0003;
                regs[6] = 0x000500f0;
                regs[7] = 0x000e0003;
                regs[8] = 0x000600f0;
                /* fallthrough */
            case NTSC_ON:
                fprintf(stderr, "CEA608 output is now on.\n");
                return 1;
            default:
                return 0;
        }
    }

    if (argc == 2 && argv[1][1] == 'f') {
        switch(state) {
            case PAL_ON:
                regs[5] = 0x00140003;
                regs[6] = 0x00020120;
                regs[7] = 0x00130003;
                regs[8] = 0x00020120;
                /* fallthrough */
            case PAL_OFF:
                fprintf(stderr, "Teletext output is now off.\n");
                return 1;
            case NTSC_ON:
                regs[5] = 0x00100003;
                regs[6] = 0x000300f0;
                regs[7] = 0x00100003;
                regs[8] = 0x000400f0;
                /* fallthrough */
            case NTSC_OFF:
                fprintf(stderr, "CEA608 output is now off.\n");
                return 1;
            default:
                return 0;
        }
    }

    fprintf(stderr, "Usage: %s (on|off)\n", argv[0]);
    return 1;
}


int main(int argc, char **argv)
{
    int fd;
    void *map_base;

    if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) {
        fprintf(stderr, "Error opening /dev/mem.\n"); exit(-1);
    }

    /* Map TV page */
    map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x20807000);

    if(map_base == (void *) -1) {
        fprintf(stderr, "Error mapping register memory.\n"); exit(-1);
    }

    if(!try_set_regs(map_base, argc, argv)) {

        map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x3f807000);

        if(map_base == (void *) -1) {
            fprintf(stderr, "Error mapping register memory.\n"); exit(-1);
        }

        if(!try_set_regs(map_base, argc, argv)) {

            map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0xfe807000);

            if(map_base == (void *) -1) {
                fprintf(stderr, "Error mapping register memory.\n"); exit(-1);
            }

            if(!try_set_regs(map_base, argc, argv)) {
                fprintf(stderr, "Could not find registers. Make sure composite video out is enabled.\n");
            }

        }

    }

    // clean up
    close(fd);
    return 0;
}

