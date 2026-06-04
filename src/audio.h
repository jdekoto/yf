#ifndef AUDIO_H
#define AUDIO_H

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include "ibxm.h"
#include "mem.h"

#define AUDIO_CHANNELS 4

// Index mappings for our split layout
#define CHAN_STREAM_1 0
#define CHAN_STREAM_2 1
#define CHAN_SFX_1    2
#define CHAN_SFX_2    3

void spu_callback(void *userdata, uint8_t *stream, int len);
void spu_init();
int  spu_feedtracker(const char* filename);

#endif
