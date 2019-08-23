/*
   teartest.c - dispmanx tearing/frame drop test
   Copyright 2019 Alistair Buxton <a.j.buxton@gmail.com>
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>

#include "bcm_host.h"

#ifndef ALIGN_UP
#define ALIGN_UP(x,y)  ((x + (y)-1) & ~((y)-1))
#endif

static DISPMANX_DISPLAY_HANDLE_T   display;
static DISPMANX_ELEMENT_HANDLE_T   element;
static DISPMANX_RESOURCE_HANDLE_T  resource[2];
static int next_resource = 0;

static unsigned short palette[256] = { 0x0, 0xffff, 0xf000 };

uint8_t *image;
VC_RECT_T image_rect;
#define TYPE (VC_IMAGE_8BPP)
#define WIDTH (32)
#define HEIGHT (32)
#define OFFSET (8)
#define PITCH (ALIGN_UP(WIDTH, 32))
#define ROW(n) (image+(PITCH*(n))+OFFSET)

pthread_cond_t cond1 = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

uint8_t inflight = 0;
int dropcount = 0;

void update_callback(DISPMANX_UPDATE_HANDLE_T u, void* arg)
{
    if (inflight > 1) {
        dropcount += 1;
        printf("Frame drop %d\n", dropcount);
    }
    inflight = 0;
    //printf("U");
}


void vsync(void)
{
    DISPMANX_UPDATE_HANDLE_T    update;

//    printf("A");
    update = vc_dispmanx_update_start( 10 );
//    printf("B");

    vc_dispmanx_element_change_source( update, element, resource[next_resource]);

    pthread_mutex_lock(&lock);
    pthread_cond_wait(&cond1, &lock);
//    printf("C");
    vc_dispmanx_update_submit( update, update_callback, NULL );
    inflight += 1;
//    printf("D\n");
    pthread_mutex_unlock(&lock);

    next_resource = next_resource ^1;
    // fill image
    memset(image, next_resource, WIDTH*HEIGHT);
    // write to resource
    vc_dispmanx_resource_write_data(  resource[next_resource], TYPE, PITCH, image, &image_rect );
}


void vsync_callback(DISPMANX_UPDATE_HANDLE_T u, void* arg)
{
//    printf("S");
    pthread_cond_signal(&cond1);
}


void *vsync_thread()
{
    while(1) vsync();
}


int main(int argc, char *argv[])
{
    VC_RECT_T       src_rect;
    VC_RECT_T       dst_rect;

    DISPMANX_UPDATE_HANDLE_T    update;
    uint32_t                    vc_image_ptr;

    bcm_host_init();

    display = vc_dispmanx_display_open( 0 );

    image = calloc( 1, PITCH * HEIGHT ); // buffer 0

    // set up some resources
    vc_dispmanx_rect_set( &image_rect, 0, 0, WIDTH, HEIGHT);
    for (int n=0;n<2;n++) {
        resource[n] = vc_dispmanx_resource_create( TYPE, WIDTH, HEIGHT, &vc_image_ptr );
        vc_dispmanx_resource_set_palette(  resource[n], palette, 0, sizeof palette );
        vc_dispmanx_resource_write_data(  resource[n], TYPE, PITCH, image, &image_rect );
    }

    update = vc_dispmanx_update_start( 10 );
    vc_dispmanx_rect_set( &src_rect, 0, 0, WIDTH << 16, HEIGHT << 16 );
    vc_dispmanx_rect_set( &dst_rect, 0, 0, WIDTH, HEIGHT );
    element = vc_dispmanx_element_add( update, display, 2000,
                                       &dst_rect, resource[0], &src_rect,
                                       DISPMANX_PROTECTION_NONE,
                                       NULL, NULL, VC_IMAGE_ROT0 );

    vc_dispmanx_update_submit_sync( update );

    pthread_t tid1;
    pthread_create(&tid1, NULL, vsync_thread, NULL);

    vc_dispmanx_vsync_callback(display, NULL, NULL);
    vc_dispmanx_vsync_callback(display, vsync_callback, NULL);

    while(1) {
        sleep(100);
    }
    return 0;
}
