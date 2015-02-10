/*
   demo.c - teletext demo
   Copyright 2015 Alistair Buxton <a.j.buxton@gmail.com>

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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "buffer.h"
#include "hamming.h"

#include "demo_buffer.h"

long double a[4] = {0, 0, 0, 0};
float cpu_f[4];

void get_cpu(void)
{
    FILE *fp;
    long double b[4], c[4], total;
    int n;

    fp = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
    fscanf(fp,"%d",&n);
    fclose(fp);

    str_parity(&buffer[ 8][25], 14, "Freq.:\x03%dMHz", n/1000);

    fp = fopen("/proc/stat", "r");
    fscanf(fp,"%*s %Lf %Lf %Lf %Lf",&b[0],&b[1],&b[2],&b[3]);
    fclose(fp);

    for (n=0; n<4; n++) {
        c[n] = b[n] - a[n];
    }

    total = c[0] + c[1] + c[2] + c[3];
    memcpy (a, b, sizeof a);

    str_parity(&buffer[10][26], 12, "User:\x03%5.1f%%", c[0]*100.0/total);
    str_parity(&buffer[11][26], 12, "Nice:\x03%5.1f%%", c[1]*100.0/total);
    str_parity(&buffer[12][26], 12, "Sys.:\x03%5.1f%%", c[2]*100.0/total);
    str_parity(&buffer[13][26], 12, "Idle:\x03%5.1f%%", c[3]*100.0/total);

}

void get_mem(void)
{
    FILE *fp;
    int mem[2];

    fp = fopen("/proc/meminfo", "r");
    fscanf(fp,"%*s %d kB\n",&mem[0]);
    fscanf(fp,"%*s %d kB\n",&mem[1]);
    fclose(fp);

    str_parity(&buffer[15][26], 12, "Mem.:\x03%4dMB", mem[0]/1024);
    str_parity(&buffer[16][26], 12, "Free:\x03%4dMB", mem[1]/1024);
}

void get_net(void)
{
    FILE *fp;
    char tmp[100];
    char *pch;
    char *tokens[4] = {0,0,0,0};
    int n;

    gethostname(tmp, 14);
    tmp[14] = 0;
    str_parity(&buffer[3][2], 15, "\x0d%14s", tmp);

    fp = popen("/sbin/ip -o -f inet addr show scope global", "r");
    fgets(tmp, 99, fp);
    pclose(fp);

    tokens[0] = pch = strtok (tmp," \n/");
    for (n=1; n<4; n++)
    {
        tokens[n] = pch = strtok (NULL, " \n/");
    }

    if(tokens[1] && tokens[3]) {
        str_parity(&buffer[3][18], 22, "%5s:\x03%s", tokens[1], tokens[3]);
    }
}

void get_temp(void)
{
    FILE *fp;
    char tmp[100];
    char *pch;

    fp = popen("/usr/bin/vcgencmd measure_temp", "r");
    fgets(tmp, 99, fp);
    pclose(fp);
    pch = strtok (tmp,"=\n");
    pch = strtok (NULL,"=\n");

    if(pch) {
        str_parity(&buffer[18][25], 14, "Temp.:\x03%6s", pch);
    }
}

void get_time(void)
{
    time_t rawtime;
    struct tm *info;
    char tmp[21];

    time( &rawtime );

    info = localtime( &rawtime );

    strftime(tmp, 21, "\x02%a %d %b\x03%H:%M/%S", info);
    str_parity(&buffer[0][22], 20, tmp);
}

void demo(void)
{
    int n, z=0;

    get_cpu();
    sleep(1);

    while(1) {
        get_time();
        if((z&0xf)==0) {
            get_cpu();
            get_mem();
            if((z&0xff) == 0) {
                get_net();
                get_temp();
            }
        }
        for( n=0; n<24; n++) {
            push_packet(buffer[n]);
        }
        usleep(100000);
        z++;
    }
}
