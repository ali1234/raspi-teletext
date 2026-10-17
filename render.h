#include <stdint.h>

#ifndef ALIGN_UP
#define ALIGN_UP(x,y) ((x + (y)-1) & ~((y)-1))
#endif

#define PITCH(w) (ALIGN_UP(w, 32))

// image is a buffer of PITCH(width) * height bytes, one byte per pixel,
// 0 = black and 1 = white. Even rows are output on one field, odd rows on
// the other. draw is called with field = 0 or 1 and must fill the rows of
// that field.
typedef void (*InitFunc)(uint8_t *image);
typedef void (*DrawFunc)(uint8_t *image, int field);

// vbi_lines is the number of lines per field to open up above the picture.
// norm is the TV standard to output ("PAL" or "NTSC"). Both are only used
// by the KMS renderer; the dispmanx renderer relies on tvctl instead.
void *render_start(int width, int height, int offset, int fixed, InitFunc init_func, DrawFunc draw_func, int delay, int level, int vbi_lines, const char *norm);
void render_stop(void *);
