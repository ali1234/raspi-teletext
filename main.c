#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>

#include "bcm_host.h"

#ifndef ALIGN_UP
#define ALIGN_UP(x,y)  ((x + (y)-1) & ~((y)-1))
#endif

/*
typedef struct
{
    DISPMANX_DISPLAY_HANDLE_T   display;
    DISPMANX_MODEINFO_T         info;
    void                       *image;
    DISPMANX_UPDATE_HANDLE_T    update;
    DISPMANX_RESOURCE_HANDLE_T  resource;
    DISPMANX_ELEMENT_HANDLE_T   element;
    uint32_t                    vc_image_ptr;

} RECT_VARS_T;

static RECT_VARS_T  gRectVars;
*/

static DISPMANX_RESOURCE_HANDLE_T  resource[3];
static int which = 0;
static void *image;

static DISPMANX_DISPLAY_HANDLE_T   display;
static DISPMANX_ELEMENT_HANDLE_T   element;


#define RGB565(r,g,b) (((r)>>3)<<11 | ((g)>>2)<<5 | (b)>>3)
static unsigned short pal[256] = {
    RGB565(0,0,0),
    RGB565(255,255,255),
    RGB565(255,0,0),
};

void vsync(DISPMANX_UPDATE_HANDLE_T u, void* arg) {

    int ret, next;
    DISPMANX_UPDATE_HANDLE_T    update;

    update = vc_dispmanx_update_start( 10 );
    assert( update );

    ret = vc_dispmanx_element_change_source( update, element, resource[which]);
    assert( ret == 0 );

    ret = vc_dispmanx_update_submit_sync( update );
    assert( ret == 0 );
    next = which ^ 1;
    which = 2; // use filler if next callback called before this one ends

    // fill image
    // write to resource

    which = next; // queue up next real resource

}

int main(void)
{
    int             ret;
    VC_RECT_T       src_rect;
    VC_RECT_T       dst_rect;
    VC_IMAGE_TYPE_T type = VC_IMAGE_8BPP;

    DISPMANX_UPDATE_HANDLE_T    update;
    uint32_t                    vc_image_ptr;

    VC_DISPMANX_ALPHA_T alpha = { DISPMANX_FLAGS_ALPHA_FROM_SOURCE | DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS,
                             255, /*alpha 0->255*/
                             0 };

    bcm_host_init();

    display = vc_dispmanx_display_open( 0 );

    int width = 360;
    int height = 16;
    int pitch = ALIGN_UP(width, 32);
    //int aligned_height = ALIGN_UP(height, 16);

    image = calloc( 1, pitch * height ); // buffer 0
    assert(image);

    vc_dispmanx_rect_set( &dst_rect, 0, 0, width, height);

    resource[0] = vc_dispmanx_resource_create( type, width, height, &vc_image_ptr );
    assert( resource[0] );
    ret = vc_dispmanx_resource_set_palette(  resource[0], pal, 0, sizeof pal );
    assert( ret == 0 );
    ret = vc_dispmanx_resource_write_data(  resource[0], type, pitch, image, &dst_rect );
    assert( ret == 0 );

    memset(image, 1, pitch * height); // buffer 1

    resource[1] = vc_dispmanx_resource_create( type, width, height, &vc_image_ptr );
    assert( resource[1] );
    ret = vc_dispmanx_resource_set_palette(  resource[1], pal, 0, sizeof pal );
    assert( ret == 0 );
    ret = vc_dispmanx_resource_write_data(  resource[1], type, pitch, image, &dst_rect );
    assert( ret == 0 );

    memset(image, 2, pitch * height); // filler

    resource[2] = vc_dispmanx_resource_create( type, width, height, &vc_image_ptr );
    assert( resource[2] );
    ret = vc_dispmanx_resource_set_palette(  resource[2], pal, 0, sizeof pal );
    assert( ret == 0 );
    ret = vc_dispmanx_resource_write_data(  resource[2], type, pitch, image, &dst_rect );
    assert( ret == 0 );

    memset(image, 1, pitch * height);


    update = vc_dispmanx_update_start( 10 );
    assert( update );



    vc_dispmanx_rect_set( &src_rect, 0, 0, width << 16, height << 16 );
    vc_dispmanx_rect_set( &dst_rect, 10, 0, 700, 64 );
    element = vc_dispmanx_element_add( update, display,
                                       2000,               // layer
                                       &dst_rect, resource[2], &src_rect,
                                       DISPMANX_PROTECTION_NONE,
                                       &alpha, NULL, VC_IMAGE_ROT0 );

    ret = vc_dispmanx_update_submit_sync( update );
    assert( ret == 0 );

    vc_dispmanx_vsync_callback(display, vsync, NULL);

    while(1) {
        sleep(1);
    }

    vc_dispmanx_vsync_callback(display, NULL, NULL); // disable callback

    update = vc_dispmanx_update_start( 10 );
    assert( update );
    ret = vc_dispmanx_element_remove( update, element );
    assert( ret == 0 );
    ret = vc_dispmanx_update_submit_sync( update );
    assert( ret == 0 );
    ret = vc_dispmanx_resource_delete( resource[0] );
    assert( ret == 0 );
    ret = vc_dispmanx_resource_delete( resource[1] );
    assert( ret == 0 );
    ret = vc_dispmanx_resource_delete( resource[2] );
    assert( ret == 0 );
    ret = vc_dispmanx_display_close( display );
    assert( ret == 0 );

    return 0;
}

