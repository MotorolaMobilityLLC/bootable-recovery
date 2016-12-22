/*
* Copyright (C) 2014 MediaTek Inc.
* Modification based on code covered by the mentioned copyright
* and/or permission notice(s).
*/


#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include "minui.h"
#include "graphics.h"

GRSurface __gr_canvas;

GRSurface* gr_canvas = NULL;
int rotate_index=-1;

static void print_surface_info(GRSurface *s, const char *name)
{
    printf("[graphics] %s > Height:%d, Width:%d, PixelBytes:%d, RowBytes:%d, Size:%d, Data: 0x%08" PRIxPTR "\n",
        name, s->height, s->width, s->pixel_bytes, s->row_bytes, s->height* s->row_bytes, (uintptr_t) s->data);
}

// Read configuration from MTK_LCM_PHYSICAL_ROTATION
#ifndef MTK_LCM_PHYSICAL_ROTATION
#define MTK_LCM_PHYSICAL_ROTATION "undefined"
#endif
static int rotate_config(GRSurface *gr_draw)
{
    if (rotate_index<0)
    {
        if (gr_draw->pixel_bytes != 4) rotate_index=0; // support 4 bytes pixel only
        else if (0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "90", 2)) rotate_index=1;
        else if (0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "180", 3)) rotate_index=2;
        else if (0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "270", 3)) rotate_index=3;
        else rotate_index=0;
        printf("[graphics] rotate_config %d %s\n", rotate_index, MTK_LCM_PHYSICAL_ROTATION);
    }
    return rotate_index;
}

#define swap(x, y, type) {type z; z=x; x=y; y=z;}

// Allocate and setup the canvas object
void rotate_canvas_init(GRSurface *gr_draw)
{
    gr_canvas = &__gr_canvas;
    memcpy(gr_canvas, gr_draw, sizeof(GRSurface));

    // Swap canvas' height and width, if the rotate angle is 90" or 270"
    if (rotate_config(gr_draw)%2) {
        swap(gr_canvas->width, gr_canvas->height, int);
        gr_canvas->row_bytes = gr_canvas->width * gr_canvas->pixel_bytes;
    }

    gr_canvas->data = (unsigned char*) malloc(gr_canvas->height * gr_canvas->row_bytes);
    if (gr_canvas->data == NULL) {
        printf("[graphics] rotate_canvas_init() malloc gr_canvas->data failed\n");
        gr_canvas = NULL;
        return;
    }

    memset(gr_canvas->data,  0, gr_canvas->height * gr_canvas->row_bytes);

    print_surface_info(gr_draw, "gr_draw");
    print_surface_info(gr_canvas, "gr_canvas");
}

// Cleanup the canvas
void rotate_canvas_exit(void)
{
    if (gr_canvas) {
        if (gr_canvas->data)
            free(gr_canvas->data);
        free(gr_canvas);
    }
    gr_canvas=NULL;
}

// Return the canvas object
GRSurface *rotate_canvas_get(GRSurface *gr_draw)
{
    // Initialize the canvas, if it was not exist.
    if (gr_canvas==NULL)
        rotate_canvas_init(gr_draw);
    return gr_canvas;
}

// Surface Rotate Routines
static void rotate_surface_0(GRSurface *dst, GRSurface *src)
{
    memcpy(dst->data, src->data, src->height*src->row_bytes);
}

static void rotate_surface_270(GRSurface *dst, GRSurface *src)
{
    int v, w, h;
    unsigned int *src_pixel;
    unsigned int *dst_pixel;

    for (h=0, v=src->width-1; h<dst->height; h++, v--) {
        for (w=0; w<dst->width; w++) {
            dst_pixel = (unsigned int *)(dst->data + dst->row_bytes*h);
            src_pixel = (unsigned int *)(src->data + src->row_bytes*w);
            *(dst_pixel+w)=*(src_pixel+v);
        }
    }
}

static void rotate_surface_180(GRSurface *dst, GRSurface *src)
{
    int v, w, k, h;
    unsigned int *src_pixel;
    unsigned int *dst_pixel;

    for (h=0, k=src->height-1; h<dst->height && k>=0 ; h++, k--) {
        dst_pixel = (unsigned int *)(dst->data + dst->row_bytes*h);
        src_pixel = (unsigned int *)(src->data + src->row_bytes*k);
        for (w=0, v=src->width-1; w<dst->width && v>=0; w++, v--) {
            *(dst_pixel+w)=*(src_pixel+v);
        }
    }
}

static void rotate_surface_90(GRSurface *dst, GRSurface *src)
{
    int w, k, h;
    unsigned int *src_pixel;
    unsigned int *dst_pixel;

    for (h=0; h<dst->height; h++) {
        for (w=0, k=src->height-1; w<dst->width; w++, k--) {
            dst_pixel = (unsigned int *)(dst->data + dst->row_bytes*h);
            src_pixel = (unsigned int *)(src->data + src->row_bytes*k);
            *(dst_pixel+w)=*(src_pixel+h);
        }
    }
}

typedef void (*rotate_surface_t) (GRSurface *, GRSurface *);

rotate_surface_t rotate_func[4]=
{
    rotate_surface_0,
    rotate_surface_90,
    rotate_surface_180,
    rotate_surface_270
};

// rotate and copy src* surface to dst surface
void rotate_surface(GRSurface *dst, GRSurface *src)
{
    rotate_surface_t rotate;
    rotate=rotate_func[rotate_config(dst)];
    rotate(dst, src);
}


