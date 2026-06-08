
// audio.c
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "audio.h"

// Unified internal sample position counters for all 4 channels
static uint32_t pcm_pos[4] = {0, 0, 0, 0};

// IBXM Context Trackers
static struct module* active_module = NULL;
static struct replay* active_replay = NULL;
static int* ibxm_mix_buf = NULL;

static int ibxm_buffer_remaining = 0;
static int ibxm_buffer_index = 0;

void spu_callback(void *userdata, uint8_t *stream, int len) {
    // Clear playback destination stream to absolute silence (128 unsigned 8-bit)
    memset(stream, 128, len);

    // Read general tracker flags directly from their dedicated memory spaces
    uint8_t tracker_enabled = memory[ADDR_TRACKER_ENABLED];
    uint8_t tracker_volume_raw = memory[ADDR_TRACKER_VOLUME];

    // --- STREAM REPLAY SAMPLES INTO CHANNEL 0 & 1 RAM ---
    if (active_replay && tracker_enabled == 1) {
        // We look at where the mixer loop currently IS (pcm_pos)
        // and fill the buffer exactly from that point forward!
        uint32_t write_pos_0 = pcm_pos[0];
        uint32_t write_pos_1 = pcm_pos[1];

        for (int i = 0; i < len; i++) {
            if (ibxm_buffer_remaining <= 0) {
                ibxm_buffer_remaining = replay_get_audio(active_replay, ibxm_mix_buf, 0);
                ibxm_buffer_index = 0;
            }

            int ibxm_left  = ibxm_mix_buf[ibxm_buffer_index * 2];
            int ibxm_right = ibxm_mix_buf[ibxm_buffer_index * 2 + 1];

            // Convert raw IBXM 16-bit signed stereo streams to standard 8-bit unsigned values
            uint8_t left_u8  = (uint8_t)(((ibxm_left  / 256) + 128) & 0xFF);
            uint8_t right_u8 = (uint8_t)(((ibxm_right / 256) + 128) & 0xFF);

            // Poke them precisely into the circular track slots
            poke(ADDR_SNDBUF + (0 * 0x8000) + write_pos_0, left_u8);
            poke(ADDR_SNDBUF + (1 * 0x8000) + write_pos_1, right_u8);

            // Advance the local write cursors safely inside the 32KB boundaries
            write_pos_0 = (write_pos_0 + 1) % 0x8000;
            write_pos_1 = (write_pos_1 + 1) % 0x8000;

            ibxm_buffer_index++;
            ibxm_buffer_remaining--;
        }

        // Set control registers so the mixer knows they are executing active data stream cycles
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
    // We divide by 256 to fit the 16-bit position safely into two 8-bit registers per channel
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

