#ifndef AUDIO_H
#define AUDIO_H

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include "mem.h"

#define AUDIO_CHANNELS 4

typedef struct {
    uint32_t start_addr;  // Memory address inside virtual RAM where sample sits
    uint32_t current_pos; // Offset tracking how many bytes have been read
    uint32_t length;      // Total size of the audio sample in bytes
    bool loop;            // Should this channel automatically loop when finished?
    bool active;          // Is this channel currently making noise?
    float volume;         // 0.0 (mute) to 1.0 (max volume)
} AudioChannel;

// Index mappings for our split layout
#define CHAN_STREAM_1 0
#define CHAN_STREAM_2 1
#define CHAN_SFX_1    2
#define CHAN_SFX_2    3

static AudioChannel channels[AUDIO_CHANNELS];

void spu_callback(void *userdata, uint8_t *stream, int len);
void spu_init();

#endif
