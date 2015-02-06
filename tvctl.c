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

int main(int argc, char **argv) {
    int fd;
    void *map_base;

    volatile unsigned int *regs;

    unsigned int values[8];

    if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) {
        printf("Error...\n"); exit(-1);
    }

    /* Map TV page */
    map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x20807000);
    if(map_base == (void *) -1) {
        printf("Error...\n"); exit(-1);
    }

    // do stuff...

    regs = map_base;

    // field timings?

    // each register is two shorts

    // eg: reg[5] = 0014 0003 - means 0x14 lines then 0x3 lines above the visible
    //     reg[6] = 0002 0120 - means 0x120 visible lines, then 0x2 lines below the display
    //     add them all you get 313 for odd field and 312 for even field, for 625 total

    // therefore, reducing the two values in reg[5] and increasing the first value in reg[6]
    // will shift the visible lines up.

    // question: what is the difference between the two values in reg[5]/reg[7] register?

    values[0] = regs[5]&0xffff;
    values[1] = (regs[5]>>16)&0xffff;
    values[2] = regs[6]&0xffff;
    values[3] = (regs[6]>>16)&0xffff;
    values[4] = regs[7]&0xffff;
    values[5] = (regs[7]>>16)&0xffff;
    values[6] = regs[8]&0xffff;
    values[7] = (regs[8]>>16)&0xffff;


    if(argc == 9) {
        int n;
        for (n=0; n< 8; n++)
            values[n] += strtoul(argv[n+1], 0, 0);

        regs[5] = values[0] | values[1]<<16;
        regs[6] = values[2] | values[3]<<16;

        regs[7] = values[4] | values[5]<<16;
        regs[8] = values[6] | values[7]<<16;

    }

    if (argc == 5) {
        int n;
        for (n=0; n< 4; n++) {
            values[n] += strtoul(argv[n+1], 0, 0);
            values[n+4] += strtoul(argv[n+1], 0, 0);
        }

        regs[5] = values[0] | values[1]<<16;
        regs[6] = values[2] | values[3]<<16;

        regs[7] = values[4] | values[5]<<16;
        regs[8] = values[6] | values[7]<<16;

    }

    printf("           C 0x%08x\n", regs[0]);
    printf("          VC 0x%08x\n", regs[1]);
    printf(" VSYNCD_EVEN 0x%08x\n", regs[2]);
    printf("       HORZA 0x%08x (%d %d)\n", regs[3], (regs[3])&0xffff, (regs[3]>>16)&0xffff);
    printf("       HORZB 0x%08x (%d %d)\n", regs[4], (regs[4])&0xffff, (regs[4]>>16)&0xffff);
    printf("       VERTA 0x%08x (%d %d)\n", regs[5], (regs[5])&0xffff, (regs[5]>>16)&0xffff);
    printf("       VERTB 0x%08x (%d %d)\n", regs[6], (regs[6])&0xffff, (regs[6]>>16)&0xffff);
    printf("  VERTA_EVEN 0x%08x (%d %d)\n", regs[7], (regs[7])&0xffff, (regs[7]>>16)&0xffff);
    printf("  VERTB_EVEN 0x%08x (%d %d)\n", regs[8], (regs[8])&0xffff, (regs[8]>>16)&0xffff);
    printf("       INTEN 0x%08x\n", regs[9]);
    printf("     INTSTAT 0x%08x\n", regs[10]);
    printf("        STAT 0x%08x\n", regs[11]);
    printf("DSI_HACT_ACT 0x%08x\n", regs[12]);


    // clean up
    close(fd);
    return 0;
}

