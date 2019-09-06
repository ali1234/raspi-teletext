#ifndef ALIGN_UP
#define ALIGN_UP(x,y) ((x + (y)-1) & ~((y)-1))
#endif

#define TYPE (VC_IMAGE_8BPP)

#define PITCH(w) (ALIGN_UP(w, 32))

typedef void (*InitFunc)(uint8_t *image);
typedef void (*DrawFunc)(uint8_t *image, int next_resource);

void *render_start(int width, int height, int offset, int fixed, InitFunc init_func, DrawFunc draw_func, int delay, int level);
void render_stop(void *);