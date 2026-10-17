/*
   render_kms.c - DRM/KMS VBI renderer

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

// Renderer for systems using the vc4 KMS driver (vc4-kms-v3d), which
// includes the Raspberry Pi 4 on current kernels. Dispmanx and tvctl do
// not work there because the kernel owns the display hardware.
//
// tvctl shifts the picture into the VBI by rewriting the PixelValve
// vertical timing registers. The vc4 driver computes those same registers
// from the KMS mode (vc4_crtc_config_pv), and the VEC encoder accepts
// modes with up to 305 (PAL) or 253 (NTSC) active lines per field
// (vc4_vec_encoder_atomic_check). So instead of poking registers, we set
// a mode that is 2*vbi_lines taller than 576i/480i, with the extra lines
// taken from the vertical back porch. For PAL with 16 lines this programs
// exactly the VBP values tvctl uses (4 and 3 lines).
//
// The VBI lines are drawn on an overlay plane covering the top of the
// screen. The console framebuffer, if there is one, is put on the primary
// plane below it, where the normal picture is.

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include "render.h"

#define MEASURE_FIELDS (100)

typedef struct dumb_fb {
    uint32_t fb_id;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
    uint8_t *map;
} dumb_fb;

typedef struct render_shared {
    int fd;
    uint32_t conn_id;
    uint32_t crtc_id;
    uint32_t primary_id;
    uint32_t plane_id;
    uint32_t plane_fb_prop;
    uint32_t mode_blob;

    dumb_fb vbi[2];
    dumb_fb black;

    pthread_t thread;
    volatile int done;

    // set by the event handlers
    int flip_pending;
    uint64_t flip_ns;
    int vblank_pending;
    uint64_t vblank_ns;

    // The vblank sequence numbers can't be used to count fields: on
    // interlaced modes the kernel computes the frame duration from the
    // already halved vtotal and halves it again, so it counts two vblanks
    // per field. Fields are counted from the vblank timestamps instead,
    // relative to field 0 at t0_ns.
    uint64_t t0_ns;
    uint64_t field_ns;
    // parity of the fields drawn from odd rows, or -1 if unknown
    int odd_parity;
    int swap;
    int xshift;

    uint8_t *image;
    int width;
    int height;
    uint32_t palette[2];
    DrawFunc draw_func;
    int delay;
} render_shared;


static uint32_t get_prop_id(int fd, uint32_t obj, uint32_t type, const char *name)
{
    uint32_t id = 0;
    drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj, type);
    if (!props)
        return 0;
    for (uint32_t i = 0; i < props->count_props && !id; i++) {
        drmModePropertyRes *p = drmModeGetProperty(fd, props->props[i]);
        if (p && strcmp(p->name, name) == 0)
            id = p->prop_id;
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);
    return id;
}


static int get_prop_value(int fd, uint32_t obj, uint32_t type, const char *name, uint64_t *value)
{
    int found = 0;
    drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj, type);
    if (!props)
        return 0;
    for (uint32_t i = 0; i < props->count_props && !found; i++) {
        drmModePropertyRes *p = drmModeGetProperty(fd, props->props[i]);
        if (p && strcmp(p->name, name) == 0) {
            *value = props->prop_values[i];
            found = 1;
        }
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);
    return found;
}


static void add_prop(drmModeAtomicReq *req, int fd, uint32_t obj, uint32_t type, const char *name, uint64_t value)
{
    uint32_t id = get_prop_id(fd, obj, type, name);
    if (id)
        drmModeAtomicAddProperty(req, obj, id, value);
    else
        fprintf(stderr, "Warning: object %u has no property \"%s\".\n", obj, name);
}


static void add_optional_prop(drmModeAtomicReq *req, int fd, uint32_t obj, uint32_t type, const char *name, uint64_t value)
{
    uint32_t id = get_prop_id(fd, obj, type, name);
    if (id)
        drmModeAtomicAddProperty(req, obj, id, value);
}


// Set an enum property by the name of the value, eg "TV mode" = "PAL".
static int add_enum_prop(drmModeAtomicReq *req, int fd, uint32_t obj, uint32_t type, const char *name, const char *value)
{
    int ret = -1;
    uint32_t id = get_prop_id(fd, obj, type, name);
    drmModePropertyRes *p = id ? drmModeGetProperty(fd, id) : NULL;
    if (!p)
        return -1;
    for (int i = 0; i < p->count_enums; i++) {
        if (strcmp(p->enums[i].name, value) == 0) {
            drmModeAtomicAddProperty(req, obj, id, p->enums[i].value);
            ret = 0;
        }
    }
    drmModeFreeProperty(p);
    return ret;
}


static void add_plane(drmModeAtomicReq *req, int fd, uint32_t plane, uint32_t crtc, uint32_t fb,
                      uint32_t src_w, uint32_t src_h, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    add_prop(req, fd, plane, DRM_MODE_OBJECT_PLANE, "FB_ID", fb);
    add_prop(req, fd, plane, DRM_MODE_OBJECT_PLANE, "CRTC_ID", crtc);
    add_prop(req, fd, plane, DRM_MODE_OBJECT_PLANE, "SRC_X", 0);
    add_prop(req, fd, plane, DRM_MODE_OBJECT_PLANE, "SRC_Y", 0);
    add_prop(req, fd, plane, DRM_MODE_OBJECT_PLANE, "SRC_W", (uint64_t)src_w << 16);
    add_prop(req, fd, plane, DRM_MODE_OBJECT_PLANE, "SRC_H", (uint64_t)src_h << 16);
    add_prop(req, fd, plane, DRM_MODE_OBJECT_PLANE, "CRTC_X", x);
    add_prop(req, fd, plane, DRM_MODE_OBJECT_PLANE, "CRTC_Y", y);
    add_prop(req, fd, plane, DRM_MODE_OBJECT_PLANE, "CRTC_W", w);
    add_prop(req, fd, plane, DRM_MODE_OBJECT_PLANE, "CRTC_H", h);
}


static int create_dumb_fb(int fd, int width, int height, dumb_fb *fb)
{
    struct drm_mode_create_dumb create = { .width = width, .height = height, .bpp = 32 };
    struct drm_mode_map_dumb map = { 0 };
    uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };

    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create))
        return -1;
    fb->handle = create.handle;
    fb->pitch = create.pitch;
    fb->size = create.size;

    handles[0] = fb->handle;
    pitches[0] = fb->pitch;
    if (drmModeAddFB2(fd, width, height, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &fb->fb_id, 0))
        return -1;

    map.handle = fb->handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map))
        return -1;
    fb->map = mmap(0, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
    if (fb->map == MAP_FAILED)
        return -1;
    memset(fb->map, 0, fb->size);
    return 0;
}


static void destroy_dumb_fb(int fd, dumb_fb *fb)
{
    struct drm_mode_destroy_dumb destroy = { .handle = fb->handle };
    if (!fb->handle)
        return;
    munmap(fb->map, fb->size);
    drmModeRmFB(fd, fb->fb_id);
    drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}


// Find a DRM device with a composite connector.
static int open_card(drmModeRes **res_out, drmModeConnector **conn_out)
{
    char path[32];
    for (int n = 0; n < 16; n++) {
        snprintf(path, sizeof path, "/dev/dri/card%d", n);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0)
            continue;
        drmModeRes *res = drmModeGetResources(fd);
        if (res) {
            for (int i = 0; i < res->count_connectors; i++) {
                drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
                if (conn && (conn->connector_type == DRM_MODE_CONNECTOR_Composite ||
                             conn->connector_type == DRM_MODE_CONNECTOR_TV)) {
                    *res_out = res;
                    *conn_out = conn;
                    return fd;
                }
                drmModeFreeConnector(conn);
            }
            drmModeFreeResources(res);
        }
        close(fd);
    }
    return -1;
}


static int find_crtc(int fd, drmModeRes *res, drmModeConnector *conn, int *index)
{
    uint32_t current = 0;
    if (conn->encoder_id) {
        drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
        if (enc) {
            current = enc->crtc_id;
            drmModeFreeEncoder(enc);
        }
    }
    for (int e = 0; e < conn->count_encoders; e++) {
        drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[e]);
        if (!enc)
            continue;
        for (int i = 0; i < res->count_crtcs; i++) {
            if ((enc->possible_crtcs & (1 << i)) && (!current || res->crtcs[i] == current)) {
                drmModeFreeEncoder(enc);
                *index = i;
                return res->crtcs[i];
            }
        }
        drmModeFreeEncoder(enc);
    }
    return 0;
}


static int plane_supports(drmModePlane *plane, uint32_t format)
{
    for (uint32_t i = 0; i < plane->count_formats; i++)
        if (plane->formats[i] == format)
            return 1;
    return 0;
}


static int find_planes(render_shared *r, int crtc_index)
{
    drmModePlaneRes *planes = drmModeGetPlaneResources(r->fd);
    if (!planes)
        return -1;
    for (uint32_t i = 0; i < planes->count_planes; i++) {
        drmModePlane *plane = drmModeGetPlane(r->fd, planes->planes[i]);
        uint64_t type;
        if (!plane)
            continue;
        if ((plane->possible_crtcs & (1 << crtc_index)) &&
            get_prop_value(r->fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, "type", &type)) {
            if (type == DRM_PLANE_TYPE_PRIMARY && !r->primary_id &&
                (plane->crtc_id == r->crtc_id || plane->crtc_id == 0))
                r->primary_id = plane->plane_id;
            else if (type == DRM_PLANE_TYPE_OVERLAY && !r->plane_id && plane->crtc_id == 0 &&
                     plane_supports(plane, DRM_FORMAT_XRGB8888))
                r->plane_id = plane->plane_id;
        }
        drmModeFreePlane(plane);
    }
    drmModeFreePlaneResources(planes);
    return (r->primary_id && r->plane_id) ? 0 : -1;
}


static void page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec,
                              unsigned int crtc_id, void *data)
{
    render_shared *r = data;
    r->flip_ns = tv_sec * 1000000000ULL + tv_usec * 1000ULL;
    r->flip_pending = 0;
}


static void sequence_handler(int fd, uint64_t sequence, uint64_t ns, uint64_t data)
{
    render_shared *r = (render_shared *)(uintptr_t)data;
    r->vblank_ns = ns;
    r->vblank_pending = 0;
}


// Process DRM events until *pending is cleared.
static int wait_event(render_shared *r, int *pending)
{
    drmEventContext ev = {
        .version = 4,
        .page_flip_handler2 = page_flip_handler,
        .sequence_handler = sequence_handler,
    };
    struct pollfd pfd = { .fd = r->fd, .events = POLLIN };

    while (*pending) {
        int ret = poll(&pfd, 1, 1000);
        if (ret < 0 && errno != EINTR)
            return -1;
        if (ret == 0) {
            fprintf(stderr, "Timed out waiting for vblank.\n");
            return -1;
        }
        if (ret > 0 && drmHandleEvent(r->fd, &ev))
            return -1;
    }
    return 0;
}


static int wait_vblank(render_shared *r)
{
    r->vblank_pending = 1;
    if (drmCrtcQueueSequence(r->fd, r->crtc_id, DRM_CRTC_SEQUENCE_RELATIVE, 1, NULL, (uintptr_t)r)) {
        fprintf(stderr, "Waiting for vblank on CRTC %u failed: %s\n", r->crtc_id, strerror(errno));
        return -1;
    }
    return wait_event(r, &r->vblank_pending);
}


static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}


static int64_t field_index(render_shared *r, uint64_t ns)
{
    return ((int64_t)(ns - r->t0_ns) + (int64_t)r->field_ns / 2) / (int64_t)r->field_ns;
}


static int compare_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}


// The two fields of an interlaced mode differ in length by one line: PAL
// has an extra line of back porch on the odd field, and NTSC an extra line
// of front porch on the even field (see vc4_crtc_config_pv). Either way,
// the longer gap between vblank interrupts is the one which ends with an
// odd field. Measuring that tells us which field is which, so we can send
// the packets in the right order.
//
// The kernel's vblank timestamps are corrected to be evenly spaced, so this
// uses the time we receive each event instead, and the median to get rid
// of scheduling delays.
static void measure_fields(render_shared *r)
{
    uint64_t ns[MEASURE_FIELDS], recv[MEASURE_FIELDS];
    uint64_t gaps[2][MEASURE_FIELDS];
    int count[2] = { 0, 0 };

    r->odd_parity = -1;
    for (int n = 0; n < MEASURE_FIELDS; n++) {
        if (wait_vblank(r)) {
            fprintf(stderr, "Warning: could not wait for vblank, field order unknown.\n");
            return;
        }
        recv[n] = now_ns();
        ns[n] = r->vblank_ns;
        if (n == 0)
            r->t0_ns = ns[0];
    }
    for (int n = 1; n < MEASURE_FIELDS; n++) {
        int64_t f = field_index(r, ns[n]);
        if (f != field_index(r, ns[n-1]) + 1)
            continue;
        gaps[f & 1][count[f & 1]++] = recv[n] - recv[n-1];
    }
    if (count[0] < 8 || count[1] < 8) {
        fprintf(stderr, "Warning: missed too many vblanks, field order unknown.\n");
        return;
    }
    qsort(gaps[0], count[0], sizeof gaps[0][0], compare_u64);
    qsort(gaps[1], count[1], sizeof gaps[1][0], compare_u64);
    double diff = (double)gaps[1][count[1] / 2] - (double)gaps[0][count[0] / 2];
    fprintf(stderr, "Field length difference: %.1f us (%d/%d samples).\n", diff / 1000.0, count[0], count[1]);
    // one line is 64us, so anything much smaller than that is just noise
    if (diff > 20000)
        r->odd_parity = 1;
    else if (diff < -20000)
        r->odd_parity = 0;
    else
        fprintf(stderr, "Warning: fields are the same length, field order unknown.\n");
}


// Which rows (0 = even, 1 = odd) are output in field number f.
static int field_rows(render_shared *r, int64_t f)
{
    int rows = (r->odd_parity < 0) ? 0 : ((int)(f & 1) == r->odd_parity);
    return rows ^ r->swap;
}


static void fill_fb(render_shared *r, dumb_fb *fb, int first_field)
{
    r->draw_func(r->image, first_field);
    r->draw_func(r->image, first_field ^ 1);
    for (int y = 0; y < r->height; y++) {
        uint8_t *src = r->image + PITCH(r->width) * y;
        uint32_t *dst = (uint32_t *)(fb->map + fb->pitch * y);
        for (int x = 0; x < r->width; x++)
            dst[x] = r->palette[src[x] & 1];
    }
}


// Each buffer holds new data for both fields and must be shown for exactly
// two fields. Depending on when the hardware latches the new display list,
// the flip event arrives at the end of the first field the buffer is shown
// in, or at the end of the field before. So we commit the next flip during
// the following field (after a delay so we are clear of the latch point),
// and if flips complete one field apart, also wait one vblank first.
void *render_thread_func(void *anon_render_shared)
{
    render_shared *r = anon_render_shared;
    int next = 1;
    int extra = 0;
    int have_last = 0;
    int flips = 0;
    int first_field = 0;
    int64_t last_field = 0;

    while (!r->done) {
        fill_fb(r, &r->vbi[next], first_field);

        if (extra && wait_vblank(r))
            break;
        usleep(r->delay);

        drmModeAtomicReq *req = drmModeAtomicAlloc();
        drmModeAtomicAddProperty(req, r->plane_id, r->plane_fb_prop, r->vbi[next].fb_id);
        r->flip_pending = 1;
        int ret = drmModeAtomicCommit(r->fd, req, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, r);
        drmModeAtomicFree(req);
        if (ret) {
            fprintf(stderr, "Page flip failed: %s\n", strerror(errno));
            break;
        }
        if (wait_event(r, &r->flip_pending))
            break;

        int64_t field = field_index(r, r->flip_ns);
        if (have_last) {
            int64_t delta = field - last_field;
            if (flips < 8)
                fprintf(stderr, "Flip %d: %lld fields after the last one, first field %s rows.\n",
                        flips, (long long)delta, first_field ? "odd" : "even");
            flips++;
            if ((delta == 1 && !extra) || (delta == 3 && extra)) {
                extra ^= 1;
                fprintf(stderr, "Flips were %lld fields apart, %s waiting for an extra vblank.\n",
                        (long long)delta, extra ? "now" : "no longer");
            }
        }
        have_last = 1;
        last_field = field;
        // the buffer we just flipped to started on the field ending at
        // this flip + extra, and the next one will start two fields later
        first_field = field_rows(r, field + extra);
        next ^= 1;
    }

    if (!r->done)
        fprintf(stderr, "Render thread stopped.\n");
    return NULL;
}


void *render_start(int width, int height, int offset, int fixed, InitFunc init_func, DrawFunc draw_func, int delay, int level, int vbi_lines, const char *norm)
{
    drmModeRes *res = NULL;
    drmModeConnector *conn = NULL;
    drmModeModeInfo mode;
    int crtc_index = 0;
    int found = 0;
    int ntsc = (strcmp(norm, "NTSC") == 0);

    // Build the render struct. Must malloc it so it is still valid
    // after this function returns.
    render_shared *r = (render_shared *)calloc(1, sizeof(render_shared));

    level = (level * 255) / 100;
    r->palette[0] = 0;
    r->palette[1] = level | level << 8 | level << 16;
    r->draw_func = draw_func;
    r->delay = (delay > -1) ? delay : 2000;
    r->swap = getenv("RASPI_TELETEXT_SWAP_FIELDS") != NULL;
    // Horizontal position of the VBI data in output pixels (74ns each).
    // The VEC starts the active line later than the firmware did, and
    // blanks the end of it, so move the data left. -20 was measured on a
    // Pi 4 to put the clock run-in where broadcasts have it.
    r->xshift = getenv("RASPI_TELETEXT_XSHIFT") ? atoi(getenv("RASPI_TELETEXT_XSHIFT")) : -20;
    r->width = width;
    r->height = height;
    r->image = calloc(1, PITCH(width) * height);
    if (!r->image) {
        fprintf(stderr, "Out of memory.\n"); exit(-1);
    }
    init_func(r->image);

    r->fd = open_card(&res, &conn);
    if (r->fd < 0) {
        fprintf(stderr, "Could not find a composite output. Make sure composite video out is enabled.\n");
        exit(-1);
    }
    if (drmSetClientCap(r->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) ||
        drmSetClientCap(r->fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
        fprintf(stderr, "The DRM driver does not support atomic modesetting.\n");
        exit(-1);
    }
    r->conn_id = conn->connector_id;
    r->crtc_id = find_crtc(r->fd, res, conn, &crtc_index);
    if (!r->crtc_id) {
        fprintf(stderr, "Could not find a CRTC for the composite output.\n");
        exit(-1);
    }
    if (find_planes(r, crtc_index)) {
        fprintf(stderr, "Could not find a free overlay plane.\n");
        exit(-1);
    }
    r->plane_fb_prop = get_prop_id(r->fd, r->plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");

    // Start from the standard interlaced mode and move the start of the
    // active area up by vbi_lines on each field.
    for (int i = 0; i < conn->count_modes && !found; i++) {
        if ((conn->modes[i].flags & DRM_MODE_FLAG_INTERLACE) &&
            conn->modes[i].hdisplay == 720 && conn->modes[i].vdisplay == (ntsc ? 480 : 576)) {
            mode = conn->modes[i];
            found = 1;
        }
    }
    if (!found) {
        fprintf(stderr, "Composite output has no 720x%di mode.\n", ntsc ? 480 : 576);
        exit(-1);
    }
    mode.vdisplay += 2 * vbi_lines;
    mode.vsync_start += 2 * vbi_lines;
    mode.vsync_end += 2 * vbi_lines;
    mode.type = DRM_MODE_TYPE_USERDEF;
    snprintf(mode.name, sizeof mode.name, "720x%di", mode.vdisplay);
    if (height > mode.vdisplay) {
        fprintf(stderr, "Image is taller than the screen.\n");
        exit(-1);
    }
    if (drmModeCreatePropertyBlob(r->fd, &mode, sizeof mode, &r->mode_blob)) {
        fprintf(stderr, "Could not create mode.\n");
        exit(-1);
    }

    for (int n = 0; n < 2; n++) {
        if (create_dumb_fb(r->fd, width, height, &r->vbi[n])) {
            fprintf(stderr, "Could not create framebuffer.\n");
            exit(-1);
        }
    }
    fill_fb(r, &r->vbi[0], 0);

    // Try to keep showing whatever was on screen, eg the console, below
    // the VBI lines. Fall back to a black picture if that is not possible.
    uint32_t picture_fb = 0, picture_w = 0, picture_h = 0;
    drmModeCrtc *crtc = drmModeGetCrtc(r->fd, r->crtc_id);
    if (crtc && crtc->buffer_id) {
        drmModeFBPtr fb = drmModeGetFB(r->fd, crtc->buffer_id);
        if (fb) {
            picture_fb = fb->fb_id;
            picture_w = fb->width;
            picture_h = fb->height;
            drmModeFreeFB(fb);
        }
    }
    drmModeFreeCrtc(crtc);

    int ret = -1;
    for (int attempt = 0; attempt < 2 && ret; attempt++) {
        if (attempt == 1 || !picture_fb) {
            if (!r->black.handle && create_dumb_fb(r->fd, 720, mode.vdisplay - 2 * vbi_lines, &r->black)) {
                fprintf(stderr, "Could not create framebuffer.\n");
                exit(-1);
            }
            picture_fb = r->black.fb_id;
            picture_w = 720;
            picture_h = mode.vdisplay - 2 * vbi_lines;
        }

        drmModeAtomicReq *req = drmModeAtomicAlloc();
        add_prop(req, r->fd, r->conn_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", r->crtc_id);
        if (add_enum_prop(req, r->fd, r->conn_id, DRM_MODE_OBJECT_CONNECTOR, "TV mode", norm))
            fprintf(stderr, "Warning: could not set TV mode to %s.\n", norm);
        add_optional_prop(req, r->fd, r->conn_id, DRM_MODE_OBJECT_CONNECTOR, "left margin", 0);
        add_optional_prop(req, r->fd, r->conn_id, DRM_MODE_OBJECT_CONNECTOR, "right margin", 0);
        add_optional_prop(req, r->fd, r->conn_id, DRM_MODE_OBJECT_CONNECTOR, "top margin", 0);
        add_optional_prop(req, r->fd, r->conn_id, DRM_MODE_OBJECT_CONNECTOR, "bottom margin", 0);
        add_prop(req, r->fd, r->crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID", r->mode_blob);
        add_prop(req, r->fd, r->crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE", 1);
        add_plane(req, r->fd, r->primary_id, r->crtc_id, picture_fb, picture_w, picture_h,
                  0, 2 * vbi_lines, mode.hdisplay, mode.vdisplay - 2 * vbi_lines);
        add_plane(req, r->fd, r->plane_id, r->crtc_id, r->vbi[0].fb_id, width, height,
                  r->xshift, 0, mode.hdisplay, height);
        ret = drmModeAtomicCommit(r->fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
        if (ret)
            fprintf(stderr, "Mode set failed: %s\n", strerror(errno));
        drmModeAtomicFree(req);
    }
    if (ret) {
        if (errno == EACCES || errno == EPERM)
            fprintf(stderr, "Another program is using the display. Stop the desktop or run from a text console.\n");
        exit(-1);
    }
    fprintf(stderr, "Output mode %s (%s) on %u VBI lines per field.\n", mode.name, norm, vbi_lines);

    uint64_t active = 0, conn_crtc = 0;
    get_prop_value(r->fd, r->crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE", &active);
    get_prop_value(r->fd, r->conn_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", &conn_crtc);
    crtc = drmModeGetCrtc(r->fd, r->crtc_id);
    fprintf(stderr, "CRTC %u (index %d): active %u, mode %s; connector %u on CRTC %u; planes %u/%u.\n",
            r->crtc_id, crtc_index, (unsigned)active, (crtc && crtc->mode_valid) ? crtc->mode.name : "none",
            r->conn_id, (unsigned)conn_crtc, r->primary_id, r->plane_id);
    drmModeFreeCrtc(crtc);

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    r->field_ns = (uint64_t)mode.htotal * mode.vtotal * 1000000ULL / mode.clock / 2;
    measure_fields(r);

    r->done = 0;

    // Start the drawing thread.
    pthread_create(&r->thread, NULL, render_thread_func, r);

    return r;
}


void render_stop(void *anon_render_shared)
{
    render_shared *r = anon_render_shared;

    r->done = 1;
    pthread_join(r->thread, NULL);

    // Closing the device drops our mode, and the kernel puts the console
    // back the way it was.
    for (int n = 0; n < 2; n++)
        destroy_dumb_fb(r->fd, &r->vbi[n]);
    destroy_dumb_fb(r->fd, &r->black);
    drmModeDestroyPropertyBlob(r->fd, r->mode_blob);
    close(r->fd);

    free(r->image);
    free(r);
}
