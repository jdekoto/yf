
// audio.c
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "audio.h"
#include "micromod.h" // Added micromod header

// Unified internal sample position counters for all 4 channels
static uint32_t pcm_pos[4] = {0, 0, 0, 0};

// Micromod Context Trackers
static signed char* mod_data_buf = NULL; // Must remain allocated during playback
static bool tracker_loaded = false;

// --- NEW INTERNAL FADE CONTROL STATES ---
static float c_tracker_volume = 1.0f;
static float c_fade_target = 1.0f;
static float c_fade_step_per_chunk = 0.0f;

void spu_callback(void *userdata, uint8_t *stream, int len) {
    
    memset(stream, 128, len);

    uint8_t tracker_enabled = memory[ADDR_TRACKER_ENABLED];

    // --- AUTOMATIC BACKGROUND C FADE TICKER ---
    if (tracker_loaded && tracker_enabled == 1) {
        if (c_tracker_volume != c_fade_target) {
            c_tracker_volume += c_fade_step_per_chunk;
            
            // Check boundary overshoots
            if ((c_fade_step_per_chunk > 0.0f && c_tracker_volume >= c_fade_target) ||
                (c_fade_step_per_chunk < 0.0f && c_tracker_volume <= c_fade_target)) {
                c_tracker_volume = c_fade_target;
                c_fade_step_per_chunk = 0.0f;
            }
            // Update the system register so memory read operations reflect the fade change
            memory[ADDR_TRACKER_VOLUME] = (uint8_t)(c_tracker_volume * 255.0f);
        }

        uint8_t tracker_volume_raw = memory[ADDR_TRACKER_VOLUME];
        uint32_t write_pos_0 = pcm_pos[0];
        uint32_t write_pos_1 = pcm_pos[1];

        short micromod_buf[len * 2];
        memset(micromod_buf, 0, sizeof(micromod_buf));
        micromod_get_audio(micromod_buf, len);

        for (int i = 0; i < len; i++) {
            int micromod_left  = micromod_buf[i * 2];
            int micromod_right = micromod_buf[i * 2 + 1];

            uint8_t left_u8  = (uint8_t)(((micromod_left  / 256) + 128) & 0xFF);
            uint8_t right_u8 = (uint8_t)(((micromod_right / 256) + 128) & 0xFF);

            poke(ADDR_SNDBUF + (0 * 0x8000) + write_pos_0, left_u8);
            poke(ADDR_SNDBUF + (1 * 0x8000) + write_pos_1, right_u8);

            write_pos_0 = (write_pos_0 + 1) % 0x8000;
            write_pos_1 = (write_pos_1 + 1) % 0x8000;
        }

        poke(CH_STATUS(0), 1);
        poke(CH_STATUS(1), 1);
        poke(CH_VOLUME(0), tracker_volume_raw);
        poke(CH_VOLUME(1), tracker_volume_raw);
    } else {
        poke(CH_STATUS(0), 0);
        poke(CH_STATUS(1), 0);
    }

    // --- STRICT UNIFIED 4-CHANNEL MIXER CORE ---
    for (int i = 0; i < len; i++) {
        float mix_sample = 0.0f;
        int active_channels = 0;

        for (int ch = 0; ch < 4; ch++) {
            // Check if the channel was explicitly triggered by a Lua function call
            if (peek(CH_TRIGGER(ch)) == 1) {
                pcm_pos[ch] = 0;        // Reset cursor to start of its 32KB chunk
                poke(CH_TRIGGER(ch), 0); // Clear hardware trigger flag
            }

            if (peek(CH_STATUS(ch)) == 0) continue;

            float volume = (float)peek(CH_VOLUME(ch)) / 255.0f;

            // Compute structural layout memory block address offset (ch * 32KB)
            uint32_t stream_addr = ADDR_SNDBUF + (ch * 0x8000) + pcm_pos[ch];
            uint8_t raw_byte = peek(stream_addr);

            // CHANNELS 2 & 3 SFX BOUNDARY CHECKING:
            if (ch >= 2) {
                uint32_t src_len = ((uint32_t)peek(CH_LEN_0(ch)) << 16) | 
                                    ((uint32_t)peek(CH_LEN_1(ch)) << 8)  | 
                                     (uint32_t)peek(CH_LEN_2(ch));

                // Advance cursor index pointer
                pcm_pos[ch]++;

                // If cursor reaches sample end or hits the max hardcoded 32KB chunk block allocation size
                if (pcm_pos[ch] >= src_len || pcm_pos[ch] >= 0x8000) {
                    if (peek(CH_LOOP(ch)) == 1) {
                        pcm_pos[ch] = 0; // Loop back to the start of this channel's 32KB chunk
                    } else {
                        poke(CH_STATUS(ch), 0); // Disable channel playback
                        pcm_pos[ch] = 0;
                    }
                }
            } else {
                // CHANNELS 0 & 1 TRACKER CHECKING:
                // Music streams advance completely aligned with the global sound driver loop index!
                pcm_pos[ch] = (pcm_pos[ch] + 1) % 0x8000;
            }

            // Convert raw unsigned 8-bit signal back into standardized float wave ranges (-1.0 to 1.0)
            float sample = ((float)raw_byte - 128.0f) / 128.0f;
            sample *= volume;

            mix_sample += sample;
            active_channels++;
            
            // Poke the current playhead indices into audio register space so Lua can see them
            poke(ADDR_AUDIO + 0x30, (uint8_t)(pcm_pos[0] & 0xFF));        // Ch0 Playhead Low Byte
            poke(ADDR_AUDIO + 0x31, (uint8_t)((pcm_pos[0] >> 8) & 0xFF)); // Ch0 Playhead High Byte

            poke(ADDR_AUDIO + 0x32, (uint8_t)(pcm_pos[1] & 0xFF));        // Ch1 Playhead Low Byte
            poke(ADDR_AUDIO + 0x33, (uint8_t)((pcm_pos[1] >> 8) & 0xFF)); // Ch1 Playhead High Byte
            
            poke(ADDR_AUDIO + 0x34, (uint8_t)(pcm_pos[2] & 0xFF));        // Ch2 Playhead Low Byte
            poke(ADDR_AUDIO + 0x35, (uint8_t)((pcm_pos[2] >> 8) & 0xFF)); // Ch2 Playhead High Byte
            
            poke(ADDR_AUDIO + 0x36, (uint8_t)(pcm_pos[3] & 0xFF));        // Ch3 Playhead Low Byte
            poke(ADDR_AUDIO + 0x37, (uint8_t)((pcm_pos[3] >> 8) & 0xFF)); // Ch3 Playhead High Byte
        }

        // Apply clean clip bounding
        if (active_channels > 1) { mix_sample /= (float)active_channels; }
        if (mix_sample > 1.0f)  mix_sample = 1.0f;
        if (mix_sample < -1.0f) mix_sample = -1.0f;

        // Write directly out to the device sound driver stream index buffer
        stream[i] = (uint8_t)((mix_sample * 128.0f) + 128.0f);
    }
}

void spu_init() {
    SDL_AudioSpec wanted;
    SDL_zero(wanted);
    
    wanted.freq = 22050;            // 22.05 kHz Sample Rate
    wanted.format = AUDIO_U8;       // Unsigned 8-bit PCM
    wanted.channels = 1;            // Strictly Mono matching our sample-per-index layout
    wanted.samples = 256;           // Lowered buffer slice for ultra-low latency audio
    wanted.callback = spu_callback;
    
    if (SDL_OpenAudio(&wanted, NULL) < 0) {
        fprintf(stderr, "Failed to open audio: %s\n", SDL_GetError());
    }
    
    SDL_PauseAudio(0); // Engage playback background thread
}

// The core engine loader - Sets up micromod at 22050 Hz
int spu_feedtracker(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) { printf("cant open %s\n", filename); return 1; }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Free the old module data *only* when loading a replacement module
    if (mod_data_buf) {
        free(mod_data_buf);
        mod_data_buf = NULL;
        tracker_loaded = false;
    }

    mod_data_buf = malloc(size);
    if (!mod_data_buf) {
        fclose(f);
        return 1;
    }

    fread(mod_data_buf, 1, size, f);
    fclose(f);

    // Initialise micromod with the raw data buffer and sampling rate
    if (micromod_initialise(mod_data_buf, 22050) < 0) {
        printf("couldnt load the module properly\n");
        free(mod_data_buf);
        mod_data_buf = NULL;
        return 1;
    }

    // Set a baseline safe mixing gain for 4-channel classic MODs
    micromod_set_gain(64);

    tracker_loaded = true;
    return 0; // Success
}

// --- NEW PROCEDURAL C API HOOKS FOR LUA ---

static char current_track_path[1024] = {0};

void spu_play_module(const char* filename, float volume) {
    // SMART INTERCEPT: If tracker is already loaded and it's the EXACT same file name...
    if (tracker_loaded && strcmp(current_track_path, filename) == 0) {
        // Just restore the hardware execution flag and reset volume parameters!
        // This acts as a completely seamless, zero-overhead resume.
        c_tracker_volume = volume;
        c_fade_target = volume;
        c_fade_step_per_chunk = 0.0f;
        
        memory[ADDR_TRACKER_VOLUME] = (uint8_t)(volume * 255.0f);
        memory[ADDR_TRACKER_ENABLED] = 1; // Wake up background thread processing
        return;
    }

    // Otherwise, this is a completely new file swap: read from disk and re-init
    if (spu_feedtracker(filename) == 0) {
        // Cache the newly loaded filename path string for future comparison checks
        strncpy(current_track_path, filename, sizeof(current_track_path) - 1);
        current_track_path[sizeof(current_track_path) - 1] = '\0';
    }
    
    c_tracker_volume = volume;
    c_fade_target = volume;
    c_fade_step_per_chunk = 0.0f;
    
    memory[ADDR_TRACKER_VOLUME] = (uint8_t)(volume * 255.0f);
    memory[ADDR_TRACKER_ENABLED] = 1;
}

void spu_pause_module(void) {
    memory[ADDR_TRACKER_ENABLED] = 0;
}

void spu_fade_module(float target, int duration_frames) {
    c_fade_target = target;
    if (duration_frames <= 0) {
        c_tracker_volume = target;
        c_fade_step_per_chunk = 0.0f;
        memory[ADDR_TRACKER_VOLUME] = (uint8_t)(target * 255.0f);
    } else {
        // Core Audio Math Transformation:
        // 22050 samples/sec / 256 samples/callback = ~86.13 callback chunks per second.
        // Assuming your engine updates at a stable 60 frames per second:
        float chunks_per_frame = 86.1328f / 60.0f; 
        float total_chunks = (float)duration_frames * chunks_per_frame;
        
        c_fade_step_per_chunk = (target - c_tracker_volume) / total_chunks;
    }
}

void spu_stop_module(void) {
    memory[ADDR_TRACKER_ENABLED] = 0; // Turn off mixer tracking
    if (tracker_loaded) {
        // Micromod native function to instantly rewind playhead to sequence position 0
        micromod_set_position(0); 
    }
}

