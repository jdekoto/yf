
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "audio.h"

static uint32_t pcm_pos[4] = {0, 0, 0, 0};

// Keeps track of the internal active tracking playhead context
static struct module* active_module = NULL;
static struct replay* active_replay = NULL;
static int* ibxm_mix_buf = NULL;

// Tracker Lifecycle States
static bool tracker_is_playing = false;

// Fade Configuration Variables
static float tracker_volume = 1.0f;       // Current runtime tracker gain multiplier
static float fade_target = 1.0f;          // The volume level we want to reach
static float fade_step_per_sample = 0.0f; // How much volume changes on every individual frame index

// Add these tracking variables to your audio global scope variables
static int ibxm_buffer_remaining = 0;
static int ibxm_buffer_index = 0;

void spu_callback(void *userdata, uint8_t *stream, int len) {
    memset(stream, 128, len);

    // Read your MMIO hardware control slots directly from your sound RAM allocation range
    uint8_t tracker_enabled = memory[ADDR_TRACKER_ENABLED];
    uint8_t tracker_volume_raw = memory[ADDR_TRACKER_VOLUME];
    float current_tracker_volume = (float)tracker_volume_raw / 255.0f;

    for (int i = 0; i < len; i++) {
        float mix_sample = 0.0f;
        int active_channels = 0;
        
        if (active_replay && tracker_enabled == 1) {
            
            // If we have exhausted our previously generated tracker samples, 
            // tell IBXM to crunch the next sequence tick immediately!
            if (ibxm_buffer_remaining <= 0) {
                // Generates a full tick and returns total stereo pairs written
                ibxm_buffer_remaining = replay_get_audio(active_replay, ibxm_mix_buf, 0);
                ibxm_buffer_index = 0;
            }

            // Read the current stereo pair safely using our index pointer
            int ibxm_left  = ibxm_mix_buf[ibxm_buffer_index * 2];
            int ibxm_right = ibxm_mix_buf[ibxm_buffer_index * 2 + 1];
            
            // Combine to mono
            float ibxm_mono = ((float)ibxm_left + (float)ibxm_right) / 2.0f;


            mix_sample += (ibxm_mono / 32768.0f) * current_tracker_volume;
            active_channels++;

            // Shift forward 1 sample pair, decrease remaining countdown pool
            ibxm_buffer_index++;
            ibxm_buffer_remaining--;
        }
        // PHASE 2: Standard Mono SFX Core (Channels 2 & 3)
        for (int ch = 2; ch < 4; ch++) {

            if (peek(CH_TRIGGER(ch)) == 1) {
                pcm_pos[ch] = 0;              // Reset internal cursor playhead
                poke(CH_TRIGGER(ch), 0);      // Acknowledge trigger by wiping it back to 0!
            }

            if (peek(CH_STATUS(ch)) == 0) continue;

            // Stitch the sample's base RAM pointer using peek blocks
            uint32_t src_addr = ((uint32_t)peek(CH_ADDR_0(ch)) << 16) | 
                                ((uint32_t)peek(CH_ADDR_1(ch)) << 8)  | 
                                 (uint32_t)peek(CH_ADDR_2(ch));

            uint32_t src_len  = ((uint32_t)peek(CH_LEN_0(ch)) << 16) | 
                                ((uint32_t)peek(CH_LEN_1(ch)) << 8)  | 
                                 (uint32_t)peek(CH_LEN_2(ch));

            float volume = (float)peek(CH_VOLUME(ch)) / 255.0f;

            uint8_t raw_byte = peek(src_addr + pcm_pos[ch]);
            
            // Convert the raw unsigned 8-bit amplitude to a floating-point wave range (-1.0 to 1.0)
            float sample = ((float)raw_byte - 128.0f) / 128.0f;
            sample *= volume;

            mix_sample += sample;
            active_channels++;

            // Safely advance the playhead forward by 1 frame
            pcm_pos[ch]++;
            if (pcm_pos[ch] >= src_len) {
                if (peek(CH_LOOP(ch)) == 1) {
                    pcm_pos[ch] = 0;
                } else {
                    poke(CH_STATUS(ch), 0); // Turn off channel on completion
                    pcm_pos[ch] = 0;
                }
            }
        }


        if (active_channels > 1) { mix_sample /= (float)active_channels; }
        if (mix_sample > 1.0f)  mix_sample = 1.0f;
        if (mix_sample < -1.0f) mix_sample = -1.0f;

        stream[i] = (uint8_t)((mix_sample * 128.0f) + 128.0f);
    }
}

void spu_init() {
    SDL_AudioSpec wanted;
    SDL_zero(wanted);
    
    wanted.freq = 22050;            // 22.05 kHz Sample Rate
    wanted.format = AUDIO_U8;       // Unsigned 8-bit PCM
    wanted.channels = 1;            // Strictly Mono matching our sample-per-index layout
    wanted.samples = 512;           // Lowered buffer slice for ultra-low latency audio
    wanted.callback = spu_callback;
    
    if (SDL_OpenAudio(&wanted, NULL) < 0) {
        fprintf(stderr, "Failed to open audio: %s\n", SDL_GetError());
    }
    
    SDL_PauseAudio(0); // Engage playback background thread
}

// The core engine loader (Same as before, sets up active_replay at 22050 Hz)
int spu_feedtracker(const char* filename) {
    char error_message[64];

    FILE* f = fopen(filename, "rb");
    if (!f) { return 0; }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* raw_data_buf = malloc(size);
    fread(raw_data_buf, 1, size, f);
    fclose(f);

    struct data module_data = { .buffer = raw_data_buf, .length = size };

    // if theres a new module requested, eradicate the currently loaded
    if (active_replay)  { dispose_replay(active_replay); active_replay = NULL; }
    if (active_module)  { dispose_module(active_module); active_module = NULL; }
    if (ibxm_mix_buf)   { free(ibxm_mix_buf); ibxm_mix_buf = NULL; }

    active_module = module_load(&module_data, error_message);
    free(raw_data_buf);

    if (!active_module) { return 0; }

    active_replay = new_replay(active_module, 22050, 1);
    if (!active_replay) { printf("couldnt load the module properly"); }
    int mix_buf_len = calculate_mix_buf_len(22050);
    ibxm_mix_buf = calloc(mix_buf_len * 4, sizeof(int));

    return 0;
}

