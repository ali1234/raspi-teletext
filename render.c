#include <assert.h>
#include <pthread.h>

#include "bcm_host.h"

#include "render.h"


typedef struct render_shared {
    DISPMANX_DISPLAY_HANDLE_T   display;
    DISPMANX_ELEMENT_HANDLE_T   element;
    DISPMANX_RESOURCE_HANDLE_T  resource[2];

    pthread_cond_t cond;
    pthread_mutex_t lock;
    pthread_t thread;
    int done;

    uint8_t *image;
    int width;
    VC_RECT_T image_rect;
    DrawFunc draw_func;
    int delay;
} render_shared;


void *render_thread_func(void *anon_render_shared)
{
    int ret;
    int next_resource = 0;
    DISPMANX_UPDATE_HANDLE_T update;
    render_shared *r = anon_render_shared;

    while(!r->done) {
        update = vc_dispmanx_update_start(10);
        assert(update);
        ret = vc_dispmanx_element_change_source(update, r->element, r->resource[next_resource]);
        assert(ret == 0);
        pthread_cond_wait(&r->cond, &r->lock);
        usleep(r->delay);
        ret = vc_dispmanx_update_submit(update, NULL, NULL );
        assert(ret == 0);
        next_resource ^= 1;
        r->draw_func(r->image, next_resource);
        ret = vc_dispmanx_resource_write_data(r->resource[next_resource], TYPE, PITCH(r->width), r->image, &r->image_rect);
        assert(ret == 0);
    }
}


void vsync_callback(DISPMANX_UPDATE_HANDLE_T u, void *anon_render_shared)
{
    render_shared *r = anon_render_shared;
    pthread_cond_signal(&r->cond);
}


void *render_start(int width, int height, int offset, int fixed, InitFunc init_func, DrawFunc draw_func, int delay)
{
    int ret;
    VC_RECT_T src_rect;
    VC_RECT_T dst_rect;

    DISPMANX_UPDATE_HANDLE_T update;
    uint32_t vc_image_ptr;
    unsigned short palette[256] = { 0x0, 0xffff, 0xf000 };

    // Build the render struct. Must malloc it so it is still valid
    // after this function returns.
    render_shared *r = (render_shared *)calloc(1, sizeof(render_shared));

    pthread_cond_init(&r->cond, NULL);
    pthread_mutex_init(&r->lock, NULL);

    r->draw_func = draw_func;
    if (delay > -1) {
        r->delay = delay;
    } else {
        r->delay = 1000;
    }
    r->width = width;
    r->image = calloc(1, PITCH(width) * height);
    assert(r->image);
    init_func(r->image);

    bcm_host_init();
    r->display = vc_dispmanx_display_open(0);

    // set up some resources
    vc_dispmanx_rect_set(&r->image_rect, 0, 0, width, height);
    for (int n=0;n<2;n++) {
        r->resource[n] = vc_dispmanx_resource_create(TYPE, width, height, &vc_image_ptr);
        assert(r->resource[n]);
        ret = vc_dispmanx_resource_set_palette(r->resource[n], palette, 0, sizeof palette);
        assert(ret == 0);
        ret = vc_dispmanx_resource_write_data(r->resource[n], TYPE, PITCH(width), r->image, &r->image_rect);
        assert(ret == 0);
    }
    vc_dispmanx_rect_set(&r->image_rect, offset+fixed, 0, width - (offset+fixed), height); // from now on, only copy the parts that change

    update = vc_dispmanx_update_start(10);
    assert(update);

    vc_dispmanx_rect_set(&src_rect, 0, 0, width << 16, height << 16);
    vc_dispmanx_rect_set(&dst_rect, 0, 0, 720, height);
    r->element = vc_dispmanx_element_add(update, r->display, 2000,
                                      &dst_rect, r->resource[2], &src_rect,
                                      DISPMANX_PROTECTION_NONE,
                                      NULL, NULL, VC_IMAGE_ROT0);

    ret = vc_dispmanx_update_submit_sync(update);
    assert(ret == 0);

    r->done = 0;

    // Start the drawing thread.
    pthread_create(&r->thread, NULL, render_thread_func, r);

    // BUG: Clear any existing callbacks, even to other apps.
    // https://github.com/raspberrypi/userland/issues/218
    // TODO: Check if we still need this.
    vc_dispmanx_vsync_callback(r->display, NULL, NULL);

    // Set the callback function.
    vc_dispmanx_vsync_callback(r->display, vsync_callback, r);

    return r;
}


void render_stop(void *anon_render_shared) {
    int ret;
    DISPMANX_UPDATE_HANDLE_T update;

    render_shared *r = anon_render_shared;

    // Stop the thread first in case it is waiting on a signal.
    r->done = 1;
    pthread_join(r->thread, NULL);

    // Stop the vsync callbacks.
    vc_dispmanx_vsync_callback(r->display, NULL, NULL);

    // Destroy element and resources.
    update = vc_dispmanx_update_start( 10 );
    assert( update );
    ret = vc_dispmanx_element_remove(update, r->element);
    assert(ret == 0);
    ret = vc_dispmanx_update_submit_sync(update);
    assert(ret == 0);
    for (int n=0; n<2; n++) {
        ret = vc_dispmanx_resource_delete(r->resource[n]);
        assert(ret == 0);
    }
    ret = vc_dispmanx_display_close(r->display);
    assert(ret == 0);

    pthread_cond_destroy(&r->cond);
    pthread_mutex_destroy(&r->lock);

    free(r);
}
