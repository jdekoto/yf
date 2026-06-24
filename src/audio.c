// audio.c
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <SDL2/SDL.h>

#include "audio.h"
#include "mem.h"

#define GATE_RAMP_SAMPLES 64


// SPU INTERNAL HARDWARE STATE REGISTRIES
static uint32_t ch_sample_cursor[4] = {0, 0, 0, 0}; // 16.16 fixed-point sample index tracking
static int32_t  ch_brr_p1[4]        = {0, 0, 0, 0}; // Filter history sample (t-1)
static int32_t  ch_brr_p2[4]        = {0, 0, 0, 0}; // Filter history sample (t-2)

// tiny 16-sample local cache per channel to hold the current active decompressed block
static int16_t  ch_brr_cache[4][16];
static int32_t  ch_current_block[4] = {-1, -1, -1, -1}; // Tracks which block index is cached

// cm sequencer data
static uint8_t* cm_data = NULL;
static uint16_t song_length = 0;
static uint8_t  track_bpm = 125;
static uint8_t  track_speed = 6;

static uint16_t current_order = 0;
static uint8_t  current_row = 0;
static uint8_t  current_tick = 0;
static uint32_t sample_tick_counter = 0;

// Tracker hardware channel effects runtime parameters
static uint16_t ch_target_pitch[4]   = {0, 0, 0, 0};
static uint8_t  ch_porta_speed[4]    = {0, 0, 0, 0};
static uint8_t  ch_vibrato_phase[4]  = {0, 0, 0, 0};
static uint8_t  ch_vibrato_speed[4]  = {0, 0, 0, 0};
static uint8_t  ch_vibrato_depth[4]  = {0, 0, 0, 0};

// Native volume fading controls
static float c_tracker_volume = 1.0f;
static float c_fade_target = 1.0f;
static float c_fade_step_per_chunk = 0.0f;

// Note frequency lookup map: 128 represents 1.0 standard playback speed (Octave 2)
// honestly we need like 5-8 octaves so we'll rework everything but pitch does infact work
static const uint8_t note_pitch_table[36] = {
    // Octave 0 (Low Bass frequencies)
    16, 17, 18, 19, 20, 21, 23, 24, 25, 27, 29, 30,
    // Octave 1
    32, 34, 36, 38, 40, 43, 45, 48, 51, 54, 57, 60,
    // Octave 2 (Base Octave: 128 is C-2 native speed)
    128, 136, 144, 152, 161, 171, 181, 192, 203, 215, 228, 242
};

// brr decoder, yes we had to cop it from the snes
static void decode_brr_block_to_cache(const uint8_t *brr_block, int16_t *out_cache, int32_t *p1, int32_t *p2) {
    uint8_t header = brr_block[0];
    uint8_t shift  = (header >> 4) & 0x0F;
    uint8_t filter = (header >> 2) & 0x03;

    if (shift >= 12) shift = 12;

    int sample_idx = 0;
    for (int i = 1; i <= 8; i++) {
        uint8_t packed_byte = brr_block[i];
        int8_t nibbles[2];
        nibbles[0] = (int8_t)(packed_byte) >> 4;
        nibbles[1] = (int8_t)(packed_byte << 4) >> 4;

        for (int n = 0; n < 2; n++) {
            int32_t step = nibbles[n];
            int32_t decoded = (step << shift) >> 1;

            switch (filter) {
                case 0: break;
                case 1: decoded += (*p1 * 15) / 16; break;
                case 2: decoded += (*p1 * 61) / 32 - (*p2 * 15) / 16; break;
                case 3: decoded += (*p1 * 115) / 64 - (*p2 * 13) / 16; break;
            }

            if (decoded > 32767)  decoded = 32767;
            if (decoded < -32768) decoded = -32768;

            out_cache[sample_idx++] = (int16_t)decoded;

            *p2 = *p1;
            *p1 = decoded;
        }
    }
}

// sequencer tick engine
void tick_tracker(void) {
    if (!cm_data || memory[ADDR_TRACKER_ENABLED] == 0) return;

    // Mathematical timing formula conversion: (Sample Rate * 2.5) / BPM
    uint32_t samples_per_tick = (22050 * 5) / (track_bpm * 2);
    sample_tick_counter++;
    
    if (sample_tick_counter < samples_per_tick) return;
    sample_tick_counter = 0; 

    // --- TICK 0: ROW EVALUATION (TRIGGER NEW NOTES / INSTRUMENTS) ---
    if (current_tick == 0) {
        uint8_t* order_list = cm_data + 8;
        uint8_t pattern_idx = order_list[current_order];
        uint8_t* pattern_ptr = cm_data + 8 + 128 + (pattern_idx * 1024);
        uint8_t* row_ptr = pattern_ptr + (current_row * 16);
        
        // poke the values somewhere a UI/game can peek them.
        memory[ADDR_AUDIO + 0x48] = current_order;
        memory[ADDR_AUDIO + 0x49] = current_row;
        memory[ADDR_AUDIO + 0x4A] = pattern_idx;

        for (int ch = 0; ch < 4; ch++) {
            uint8_t note  = row_ptr[(ch * 5) + 0];
            uint8_t inst  = row_ptr[(ch * 5) + 1];
            uint8_t vol   = row_ptr[(ch * 5) + 2];
            uint8_t eff   = row_ptr[(ch * 5) + 3];
            uint8_t param = row_ptr[(ch * 5) + 4];

            if (inst > 0 && inst <= 64 ) {
                uint32_t slot = inst - 1;
                uint32_t header_ptr = ADDR_SNDBNK + (slot * 7);
                
                memory[CH_ADDR_LO(ch)] = memory[header_ptr + 0];
                memory[CH_ADDR_HI(ch)] = memory[header_ptr + 1];
                memory[CH_LEN_LO(ch)]  = memory[header_ptr + 2];
                memory[CH_LEN_HI(ch)]  = memory[header_ptr + 3];
                memory[CH_VOLUME(ch)]  = memory[header_ptr + 4];
                memory[CH_LOOP(ch)]    = memory[header_ptr + 6] & 0x01;
                memory[CH_STATUS(ch)]  = (memory[header_ptr + 6] & 0x02) ? 2 : 1;
            }
            
            // Direct Volume Column Evaluation
            if (vol != 0xFF) {
                memory[CH_VOLUME(ch)] = vol;
            }

            uint16_t target_pitch = note_pitch_table[note - 1];
            if (note > 0 && note <= 36) {
                if (eff == 0x03) {
                    ch_target_pitch[ch] = target_pitch;
                } else {
                    memory[CH_PITCH(ch)] = (uint8_t)target_pitch;
                    memory[CH_TRIGGER(ch)] = 1; // Pulse sound engine gate high
                }
            }

            if (eff == 0x03 && memory[CH_STATUS(ch)] != 0) {
                ch_target_pitch[ch] = target_pitch;   /* already playing — glide */
            } else {
                memory[CH_PITCH(ch)] = (uint8_t)target_pitch;
            }
            if (eff == 0x04) {
                if ((param >> 4) > 0) ch_vibrato_speed[ch] = (param >> 4);
                if ((param & 0x0F) > 0) ch_vibrato_depth[ch] = (param & 0x0F);
            }
        }
    } 
    // --- TICKS 1+: SUB-ROW RUNTIME MODIFIERS ---
    else {
        uint8_t* order_list = cm_data + 8;
        uint8_t pattern_idx = order_list[current_order];
        uint8_t* pattern_ptr = cm_data + 8 + 128 + (pattern_idx * 1024);
        uint8_t* row_ptr = pattern_ptr + (current_row * 16);

        for (int ch = 0; ch < 4; ch++) {
            uint8_t eff   = row_ptr[(ch * 5) + 3];
            uint8_t param = row_ptr[(ch * 5) + 4];

            uint16_t cur_pitch = memory[CH_PITCH(ch)];
            uint8_t  cur_vol   = memory[CH_VOLUME(ch)];

            // we WILL add more effects but for right now, thug it out/
            switch (eff) {
                case 0x01: // Portamento Up
                    cur_pitch += param;
                    break;
                case 0x02: // Portamento Down
                    if (cur_pitch > param) cur_pitch -= param;
                    else cur_pitch = 128;
                    break;
                case 0x03: // Tone Portamento
                    if (cur_pitch < ch_target_pitch[ch]) {
                        cur_pitch += ch_porta_speed[ch];
                        if (cur_pitch > ch_target_pitch[ch]) cur_pitch = ch_target_pitch[ch];
                    } else if (cur_pitch > ch_target_pitch[ch]) {
                        if (cur_pitch > ch_porta_speed[ch]) cur_pitch -= ch_porta_speed[ch];
                        if (cur_pitch < ch_target_pitch[ch]) cur_pitch = ch_target_pitch[ch];
                    }
                    break;
                case 0x04: // Vibrato
                    ch_vibrato_phase[ch] += ch_vibrato_speed[ch];
                    int8_t sine_mod = (ch_vibrato_phase[ch] & 0x08) ? 1 : -1;
                    cur_pitch += (sine_mod * (ch_vibrato_depth[ch] * 4));
                    break;
                case 0x0A: // Volume Slide
                    if ((param >> 4) > 0) {
                        if (cur_vol + (param >> 4) <= 255) cur_vol += (param >> 4);
                    } else if ((param & 0x0F) > 0) {
                        if (cur_vol >= (param & 0x0F)) cur_vol -= (param & 0x0F);
                    }
                    break;
            }

            if (cur_pitch == 0) cur_pitch = 128;
            // (Note: To unlock 8 full octaves, change this to a 16-bit write across byte 8 & 9!)
            memory[CH_PITCH(ch)] = (uint8_t)(cur_pitch > 255 ? 255 : cur_pitch);
            memory[CH_VOLUME(ch)] = cur_vol;
        }
    }

    current_tick++;
    if (current_tick >= track_speed) {
        current_tick = 0;
        current_row++;
        if (current_row >= 64) {
            current_row = 0;
            current_order++;
            if (current_order >= song_length) current_order = 0;
        }
    }
}

// hardware output
void spu_callback(void *userdata, uint8_t *stream, int len) {
    uint8_t tracker_enabled = memory[ADDR_TRACKER_ENABLED];

    if (cm_data && tracker_enabled == 1) {
        if (c_tracker_volume != c_fade_target) {
            c_tracker_volume += c_fade_step_per_chunk;
            if ((c_fade_step_per_chunk > 0.0f && c_tracker_volume >= c_fade_target) ||
                (c_fade_step_per_chunk < 0.0f && c_tracker_volume <= c_fade_target)) {
                c_tracker_volume = c_fade_target;
                c_fade_step_per_chunk = 0.0f;
            }
            memory[ADDR_TRACKER_VOLUME] = (uint8_t)(c_tracker_volume * 255.0f);
        }
    }

    for (int i = 0; i < len; i++) {
        tick_tracker(); 

        int32_t accum = 0;
        for (int ch = 0; ch < 4; ch++) {
        // Keep a local static cache of what mode (PCM vs BRR) each channel is configured for
            static uint8_t ch_persisted_mode[4] = {1, 1, 1, 1};

            if (memory[CH_TRIGGER(ch)] == 1) {
                ch_sample_cursor[ch] = 0;
                ch_brr_p1[ch] = 0;
                ch_brr_p2[ch] = 0;
                ch_current_block[ch] = -1; 
                memory[CH_TRIGGER(ch)] = 0;

                // If the channel was turned off, wake it back up using its last known playback mode!
                if (memory[CH_STATUS(ch)] == 0) {
                    memory[CH_STATUS(ch)] = ch_persisted_mode[ch];
                }
            }

            // Capture the active status now that trigger wakeups have had their say
            uint8_t status = memory[CH_STATUS(ch)];
            if (status == 0) continue; 

            // Save the active state whenever an explicit instrument changes it
            ch_persisted_mode[ch] = status;

            uint32_t sample_start = ((uint32_t)memory[CH_ADDR_HI(ch)] << 8) | memory[CH_ADDR_LO(ch)];
            uint32_t sample_len   = ((uint32_t)memory[CH_LEN_HI(ch)] << 8) | memory[CH_LEN_LO(ch)];
            uint32_t vol          = memory[CH_VOLUME(ch)];
            uint32_t pitch        = memory[CH_PITCH(ch)]; 
            bool     looping      = (memory[CH_LOOP(ch)] == 1);

            uint32_t base_ram_addr = ADDR_SNDBNK + sample_start;
            uint32_t total_samples = (status == 2) ? (sample_len * 16) : sample_len;
            if (pitch == 0) pitch = 128; 

            uint32_t target_sample_idx = (ch_sample_cursor[ch] >> 16);

            if (target_sample_idx >= total_samples) {
                if (looping) {
                    ch_sample_cursor[ch] = 0;
                    if (status == 2) {
                        ch_brr_p1[ch] = 0;
                        ch_brr_p2[ch] = 0;
                        ch_current_block[ch] = -1;
                    }
                    target_sample_idx = 0;
                } else {
                    memory[CH_STATUS(ch)] = 0; 
                    continue;
                }
            }
            
            // unnessacary but i wanted to give all the joy of streaming wav files ;)
            uint8_t half = (target_sample_idx < (total_samples >> 1)) ? 0 : 1;
            memory[CH_BUF_HALF(ch)] = half;

            int32_t signed_sample = 0;
            
            /* ─── MODE 1: RAW 8-BIT PCM STREAMING ─── */
            if (status == 1) {
                signed_sample = (int32_t)memory[base_ram_addr + target_sample_idx] - 128;
            }
            
            /* ─── MODE 2: REAL-TIME 4-BIT BRR DECOMPRESSION ─── */
            else if (status == 2) {
                int32_t required_block_idx = target_sample_idx / 16;
                int     sample_sub_idx     = target_sample_idx % 16;

                if (ch_current_block[ch] != required_block_idx) {
                    uint32_t block_ram_addr = base_ram_addr + (required_block_idx * 9);
                    decode_brr_block_to_cache(
                        &memory[block_ram_addr], 
                        ch_brr_cache[ch], 
                        &ch_brr_p1[ch], 
                        &ch_brr_p2[ch]
                    );
                    ch_current_block[ch] = required_block_idx;
                }

                int16_t raw_brr_pcm = ch_brr_cache[ch][sample_sub_idx];
                signed_sample = (int32_t)(raw_brr_pcm >> 8);
            }

            /* ─── OPTIMIZED INTEGER SOFTWARE GATE RAMP ─── */
            uint32_t active_vol = vol;
            if (!looping) {
                uint32_t remaining = total_samples - target_sample_idx;
                if (remaining <= GATE_RAMP_SAMPLES && total_samples > GATE_RAMP_SAMPLES) {
                    active_vol = (vol * remaining) / GATE_RAMP_SAMPLES;
                }
            }

            signed_sample = (signed_sample * (int32_t)active_vol) / 255;
            accum += signed_sample;

            // Increment the playhead step counter matching your precise pitch rule
            ch_sample_cursor[ch] += (pitch << 9);
        }

        // Apply global background tracking master attenuation if module execution is running
        if (tracker_enabled == 1) {
            accum = (accum * (int32_t)memory[ADDR_TRACKER_VOLUME]) / 255;
        }

        int32_t mixed_output = 128 + accum;
        if (mixed_output > 255) mixed_output = 255;
        if (mixed_output < 0)   mixed_output = 0;

        stream[i] = (uint8_t)mixed_output;
        
        // unnessacary too but its for visualizers
        static uint8_t viz_write_ptr = 0;
        memory[ADDR_AUDIO + 0x40 + viz_write_ptr] = (uint8_t)mixed_output;
        viz_write_ptr = (viz_write_ptr + 1) % 128;
    
    }
}

void spu_init(void) {
    SDL_AudioSpec wanted;
    SDL_zero(wanted);
    
    wanted.freq = 22050;            
    wanted.format = AUDIO_U8;       
    wanted.channels = 1;            
    wanted.samples = 256;           
    wanted.callback = spu_callback;
    
    if (SDL_OpenAudio(&wanted, NULL) < 0) {
        fprintf(stderr, "Failed to open audio: %s\n", SDL_GetError());
    }
    SDL_PauseAudio(0); 
}

// the ENTIRE hardware tracker is a WIP, mainly since i dont have a tracker ready for the format
void spu_play_module(const char* filename, float volume) {
    FILE* f = fopen(filename, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (cm_data) free(cm_data);
    cm_data = (uint8_t*)malloc(size);
    fread(cm_data, 1, size, f);
    fclose(f);

    track_bpm = cm_data[4];
    track_speed = cm_data[5];
    song_length = cm_data[6] | (cm_data[7] << 8);

    current_order = 0;
    current_row = 0;
    current_tick = 0;
    sample_tick_counter = 0;

    c_tracker_volume = volume;
    c_fade_target = volume;
    c_fade_step_per_chunk = 0.0f;

    memory[ADDR_TRACKER_VOLUME] = (uint8_t)(volume * 255.0f);
    memory[ADDR_TRACKER_ENABLED] = 1;
}

// does NOT work
void spu_pause_module(void) {
    memory[ADDR_TRACKER_ENABLED] = 0;
}

// untested
void spu_fade_module(float target, int duration_frames) {
    c_fade_target = target;
    if (duration_frames <= 0) {
        c_tracker_volume = target;
        c_fade_step_per_chunk = 0.0f;
        memory[ADDR_TRACKER_VOLUME] = (uint8_t)(target * 255.0f);
    } else {
        float total_chunks = (float)duration_frames * (86.1328f / 60.0f);
        c_fade_step_per_chunk = (target - c_tracker_volume) / total_chunks;
    }
}

void spu_stop_module(void) {
    memory[ADDR_TRACKER_ENABLED] = 0;
    for (int ch = 0; ch < 4; ch++) {
        memory[CH_STATUS(ch)] = 0;
        ch_sample_cursor[ch] = 0;
    }
    current_order = 0;
    current_row = 0;
    current_tick = 0;
}

