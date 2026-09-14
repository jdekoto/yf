#ifndef SNAP_H
#define SNAP_H

#include "mem.h"
#include <stdio.h>
#include <stdbool.h>

extern uint16_t framebuf[FB_WID * FB_HEI];

void screenshot(void);
void toggle_gif(void);
void gif_tick(void);
#endif
