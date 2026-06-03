#include <math.h>
#include "audio.h"

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

static uint32_t pcm_pos[4] = {0, 0, 0, 0};

void spu_callback(void *userdata, uint8_t *stream, int len) {
    memset(stream, 128, len); 

    for (int i = 0; i < len; i += 2) {
        float mix_left = 0.0f;
        float mix_right = 0.0f;
        int active_channels = 0;

        for (int ch = 0; ch < 4; ch++) {
            
            // --- PURE MEMORY HARDWARE TRIGGER CHECK ---
            if (memory[CH_TRIGGER(ch)] == 1) {
                pcm_pos[ch] = 0;              // Reset internal cursor playhead
                memory[CH_TRIGGER(ch)] = 0;   // Acknowledge trigger by wiping it back to 0!
            }

            if (memory[CH_STATUS(ch)] == 0) continue;

            uint32_t src_addr = (memory[CH_ADDR_0(ch)] << 16) | 
                                (memory[CH_ADDR_1(ch)] << 8)  | 
                                 memory[CH_ADDR_2(ch)];

            uint32_t src_len = (memory[CH_LEN_0(ch)] << 16) | 
                               (memory[CH_LEN_1(ch)] << 8)  | 
                                memory[CH_LEN_2(ch)];

            float volume = (float)memory[CH_VOLUME(ch)] / 255.0f;

            uint8_t raw_byte = memory[src_addr + pcm_pos[ch]];
            float sample = ((float)raw_byte - 128.0f) / 128.0f;
            sample *= volume;

            if (ch == 0 || ch == 2) { mix_left += sample * 0.9f; mix_right += sample * 0.4f; }
            else                    { mix_left += sample * 0.4f; mix_right += sample * 0.9f; }
            active_channels++;

            pcm_pos[ch]++;
            if (pcm_pos[ch] >= src_len) {
                if (memory[CH_LOOP(ch)] == 1) {
                    pcm_pos[ch] = 0; 
                } else {
                    memory[CH_STATUS(ch)] = 0; 
                    pcm_pos[ch] = 0;
                }
            }
        }

        if (active_channels > 1) { mix_left /= active_channels; mix_right /= active_channels; }
        if (mix_left > 1.0f) mix_left = 1.0f; if (mix_left < -1.0f) mix_left = -1.0f;
        if (mix_right > 1.0f) mix_right = 1.0f; if (mix_right < -1.0f) mix_right = -1.0f;

        stream[i]     = (uint8_t)((mix_left  * 128.0f) + 128.0f);
        stream[i + 1] = (uint8_t)((mix_right * 128.0f) + 128.0f);
    }
}

void spu_init() {
    SDL_AudioSpec wanted;
    SDL_zero(wanted);
    
    wanted.freq = 22050;          // Sample rate (22.05 kHz)
    wanted.format = AUDIO_U8;     // Unsigned 8-bit PCM samples
    wanted.channels = 1;          // Mono sound
    wanted.samples = 1024;         // Buffer size chunk
    wanted.callback = spu_callback; // Point it to our memory-peeking callback
    
    if (SDL_OpenAudio(&wanted, NULL) < 0) {
        fprintf(stderr, "Failed to open audio: %s\n", SDL_GetError());
    }
    
    // Start playing audio processing in the background
    SDL_PauseAudio(0); 
}
