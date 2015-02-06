#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>

#include "bcm_host.h"

#include "hamming.h"

#ifndef ALIGN_UP
#define ALIGN_UP(x,y)  ((x + (y)-1) & ~((y)-1))
#endif

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



int main(void)
{
    RECT_VARS_T    *vars;
    uint32_t        screen = 0;
    int             ret;
    VC_RECT_T       src_rect;
    VC_RECT_T       dst_rect;
    VC_IMAGE_TYPE_T type = VC_IMAGE_RGB565;

    VC_DISPMANX_ALPHA_T alpha = { DISPMANX_FLAGS_ALPHA_FROM_SOURCE | DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS, 
                             255, /*alpha 0->255*/
                             0 };

    unsigned char packet[45] = {0x55, 0x55, 0x27};

    // mrag
    packet[3] = hamming84(1);
    packet[4] = hamming84(0);

    // page-subpage
    packet[5] = hamming84(0);
    packet[6] = hamming84(0);
    packet[7] = hamming84(0);
    packet[8] = hamming84(0);
    packet[9] = hamming84(0);
    packet[10] = hamming84(0);
    packet[11] = hamming84(0);
    packet[12] = hamming84(0);

    char message[32] = "Hello Raspberry Pi ABCDEFGHIJ";
    int z;
    for (z=0; z<32;z++)
        packet[13+z] = parity(message[z]);

    vars = &gRectVars;

    bcm_host_init();

    printf("Open display[%i]...\n", screen );
    vars->display = vc_dispmanx_display_open( screen );

    ret = vc_dispmanx_display_get_info( vars->display, &vars->info);
    assert(ret == 0);
    printf( "Display is %d x %d\n", vars->info.width, vars->info.height );

    int width = 360;
    int height = 1;
    int pitch = ALIGN_UP(width*2, 32);
    int aligned_height = ALIGN_UP(height, 16);

    vars->image = calloc( 1, pitch * height );
    assert(vars->image);

    int x, xx, n;

    for(x = 0;x < 45; x+=1) {
        for (xx = 0; xx < 8; xx++) {
            n = 2 * ((x * 8) + xx);
            if ((packet[x] >> xx)&1) {
                ((unsigned char *)(vars->image))[n] = 0xff;
                ((unsigned char *)(vars->image))[n+1] = 0xff;
            } else {
                ((unsigned char *)(vars->image))[n] = 0x0;
                ((unsigned char *)(vars->image))[n+1] = 0x0;
            }
        }
    }

    vars->resource = vc_dispmanx_resource_create( type,
                                                  width,
                                                  height,
                                                  &vars->vc_image_ptr );
    assert( vars->resource );



    vc_dispmanx_rect_set( &dst_rect, 0, 0, width, height);
    ret = vc_dispmanx_resource_write_data(  vars->resource,
                                            type,
                                            pitch,
                                            vars->image,
                                            &dst_rect );
    assert( ret == 0 );

    vars->update = vc_dispmanx_update_start( 10 );
    assert( vars->update );



    vc_dispmanx_rect_set( &src_rect, 0, 0, width << 16, height << 16 );
    vc_dispmanx_rect_set( &dst_rect, 10, 0, 700, 64 );
    vars->element = vc_dispmanx_element_add(    vars->update,
                                                vars->display,
                                                2000,               // layer
                                                &dst_rect,
                                                vars->resource,
                                                &src_rect,
                                                DISPMANX_PROTECTION_NONE,
                                                &alpha,
                                                NULL,             // clamp
                                                VC_IMAGE_ROT0 );

    ret = vc_dispmanx_update_submit_sync( vars->update );
    assert( ret == 0 );


    while(1) {

/*        for(x = 0;x < 45; x+=1) {
            for (xx = 0; xx < 8; xx++) {
                n = 2 * ((x * 8) + xx);
                if ((packet[x] >> xx)&1) {
                    ((unsigned char *)(vars->image))[n] = 0xff;
                    ((unsigned char *)(vars->image))[n+1] = 0xff;
                } else {
                    ((unsigned char *)(vars->image))[n] = 0x0;
                    ((unsigned char *)(vars->image))[n+1] = 0x0;
                }
            }
        }
*/

        sleep(1);

    }


    vars->update = vc_dispmanx_update_start( 10 );
    assert( vars->update );
    ret = vc_dispmanx_element_remove( vars->update, vars->element );
    assert( ret == 0 );
    ret = vc_dispmanx_update_submit_sync( vars->update );
    assert( ret == 0 );
    ret = vc_dispmanx_resource_delete( vars->resource );
    assert( ret == 0 );
    ret = vc_dispmanx_display_close( vars->display );
    assert( ret == 0 );

    return 0;
}

