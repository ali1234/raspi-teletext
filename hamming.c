/* Hamming and parity calculations */

/* Copyright 2015 Alistair Buxton <a.j.buxton@gmail.com> */

unsigned char hamming84(unsigned char d)
{

    unsigned char d1 = d&1;
    unsigned char d2 = (d>>1)&1;
    unsigned char d3 = (d>>2)&1;
    unsigned char d4 = (d>>3)&1;

    unsigned char p1 = (1 + d1 + d3 + d4) & 1;
    unsigned char p2 = (1 + d1 + d2 + d4) & 1;
    unsigned char p3 = (1 + d1 + d2 + d3) & 1;
    unsigned char p4 = (1 + p1 + d1 + p2 + d2 + p3 + d3 + d4) & 1;

    return (p1 | (d1<<1) | (p2<<2) | (d2<<3) 
     | (p3<<4) | (d3<<5) | (p4<<6) | (d4<<7));

}

unsigned char parity(unsigned char d)
{

    d &= 0x7f;
    unsigned char p = 1;
    unsigned char t = d;
    int i;
    for (i=0; i<7; i++) {
        p += t&1;
        t = t>>1;
    }
    p &= 1;
    return d|(p<<7);

}
