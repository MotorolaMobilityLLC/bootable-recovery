/*
* Copyright (C) 2014 MediaTek Inc.
* Modification based on code covered by the mentioned copyright
* and/or permission notice(s).
*/

#ifndef MT_GRAPHICS_ROTATE_H_
#define MT_GRAPHICS_ROTATE_H_

#include "minui.h"

void rotate_canvas_exit(void);
void rotate_canvas_init(GRSurface *gr_draw);
void rotate_surface(GRSurface *dst, GRSurface *src);
GRSurface *rotate_canvas_get(GRSurface *gr_draw);

#endif
