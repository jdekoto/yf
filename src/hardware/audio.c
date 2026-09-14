// audio.c
// TODO: make adsr customizable for each channel
// didnt want to say this but we may need to make aputools
// i dont want to make like a gui, i wanna do a cli/tui 
// interface. johnnovak's nim-mod repo seems like a great
// place to reference, or atleast borrow the rendering code
// from
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include "headers/audio.h"

#define GATE_RAMP_SAMPLES 64

// ─── ADSR HARDWARE ENVELOPE CONFIGURATION ───
typedef enum {
    ADSR_IDLE,
    ADSR_ATTACK,
    ADSR_DECAY,
    ADSR_SUSTAIN,
    ADSR_RELEASE
} adsr_state_t;

// State registries per hardware channel
static adsr_state_t ch_adsr_state[4]     = {ADSR_IDLE, ADSR_IDLE, ADSR_IDLE, ADSR_IDLE};
static int32_t      ch_adsr_volume[4]    = {0, 0, 0, 0};       // High precision: 0 to 65536
static uint32_t     ch_adsr_attack[4]    = {1200, 1200, 1200, 1200}; // Envelope increment per sample
static uint32_t     ch_adsr_decay[4]     = {400, 400, 400, 400};   // Envelope decrement per sample
static int32_t      ch_adsr_sustain[4]   = {45000, 45000, 45000, 45000}; // Sustain amplitude floor boundary
static uint32_t     ch_adsr_release[4]   = {350, 350, 350, 350};   // Envelope decrement during Note-Off

// SPU INTERNAL HARDWARE STATE REGISTRIES
static uint32_t ch_sample_cursor[4] = {0, 0, 0, 0}; // 16.16 fixed-point sample index tracking
static int32_t  ch_brr_p1[4]        = {0, 0, 0, 0}; // Filter history sample (t-1)
static int32_t  ch_brr_p2[4]        = {0, 0, 0, 0}; // Filter history sample (t-2)

// Loop cache optimization targets (Populated on instrument load to eliminate inner-loop overhead)
static uint32_t ch_loop_start_frame[4] = {0, 0, 0, 0};
static uint32_t ch_loop_end_frame[4]   = {0, 0, 0, 0};

// Tiny 16-sample local cache per channel to hold the current active decompressed block
static int16_t  ch_brr_cache[4][16];
static int32_t  ch_current_block[4] = {-1, -1, -1, -1}; // Tracks which block index is cached

// CM Sequencer metadata
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
static float c_fade_step_per_sample = 0.0f;

static const uint8_t note_pitch_table[36] = {
    // Octave 1
    24, 26, 27, 29, 31, 32, 34, 36, 39, 41, 43, 46,
    // Octave 2
    49, 52, 55, 58, 61, 65, 69, 73, 77, 82, 87, 92,
    // Octave 3 (C-3 = 97 = 8363 Hz @ 22050 Hz APU rate)
    97, 103, 109, 116, 123, 130, 138, 146, 155, 164, 174, 184
};

// Bit-exact SNES BRR decompression engine
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

// Sequencer routing clock with fully implemented MOD channel effects
void tick_tracker(void) {
    if (!cm_data || memory[TRACKER_ENABLED] == 0) return;

    uint32_t samples_per_tick = (22050 * 5) / (track_bpm * 2);
    sample_tick_counter++;
    
    if (sample_tick_counter < samples_per_tick) return;
    sample_tick_counter = 0;

    uint8_t* order_list = cm_data + 8;
    uint8_t pattern_idx = order_list[current_order];
    uint8_t* pattern_ptr = cm_data + 8 + 128 + (pattern_idx * 1024);
    uint8_t* row_ptr = pattern_ptr + (current_row * 16);

    // ─── TICK 0: ROW EVALUATION (TRIGGER ENVELOPES / SAMPLES / INITIAL EFFECTS) ───
    if (current_tick == 0) {
        memory[ADDR_AUDIO + 0x48] = current_order;
        memory[ADDR_AUDIO + 0x49] = current_row;
        memory[ADDR_AUDIO + 0x4A] = pattern_idx;

        for (int ch = 0; ch < 4; ch++) {
            uint8_t note  = row_ptr[(ch * 4) + 0];
            uint8_t inst  = row_ptr[(ch * 4) + 1];
            uint8_t eff   = row_ptr[(ch * 4) + 2];
            uint8_t param = row_ptr[(ch * 4) + 3];

            // 1. Instrument Load & Initial Setup
            if (inst > 0 && inst <= 64) {
                uint32_t slot = inst - 1;
                uint32_t header_ptr = ADDR_SNDBNK + (slot * 12);
                
                uint32_t sample_offset = *(uint32_t*)&memory[header_ptr + 0];
                uint32_t sample_length = *(uint32_t*)&memory[header_ptr + 4];
                uint16_t loop_start    = *(uint16_t*)&memory[header_ptr + 8];
                uint8_t  def_volume    = memory[header_ptr + 10];
                uint8_t  flags         = memory[header_ptr + 11];

                memory[CH_ADDR_LO(ch)] = (uint8_t)(sample_offset & 0xFF);
                memory[CH_ADDR_HI(ch)] = (uint8_t)((sample_offset >> 8) & 0xFF);
                memory[CH_LEN_LO(ch)]  = (uint8_t)(sample_length & 0xFF);
                memory[CH_LEN_HI(ch)]  = (uint8_t)((sample_length >> 8) & 0xFF);
                memory[CH_VOLUME(ch)]  = def_volume;
                memory[CH_LOOP(ch)]    = flags & 0x01;
                memory[CH_STATUS(ch)]  = (flags & 0x02) ? 2 : 1;

                ch_loop_start_frame[ch] = (uint32_t)loop_start;
                ch_loop_end_frame[ch]   = (flags & 0x02) ? (sample_length * 16) : sample_length;

                // Tone Portamento (0x03) prevents envelope restart for smooth legato
                if (eff != 0x03) {
                    ch_adsr_state[ch]  = ADSR_ATTACK;
                    ch_adsr_volume[ch] = 0;
                }
            }
            
            // Note-Off Handler (Note 0xFF)
            if (note == 0xFF) {
                ch_adsr_state[ch] = ADSR_RELEASE;
            }

            // Tick-0 Effect Initialization
            switch (eff) {
                case 0x0C: // Set Volume (0x00 - 0xFF)
                    memory[CH_VOLUME(ch)] = param;
                    break;
                case 0x08: // Set ADSR Attack (High Nibble) & Release (Low Nibble)
                    if ((param >> 4) > 0)   ch_adsr_attack[ch]  = (param >> 4) * 150;
                    if ((param & 0x0F) > 0) ch_adsr_release[ch] = (param & 0x0F) * 50;
                    break;
                case 0x09: // Set ADSR Sustain Level
                    ch_adsr_sustain[ch] = (int32_t)param * 257;
                    break;
                case 0x03: // Tone Portamento Setup (Save slide speed)
                    if (param > 0) ch_porta_speed[ch] = param;
                    break;
                case 0x04: // Vibrato Setup (Save speed & depth)
                    if ((param >> 4) > 0)   ch_vibrato_speed[ch] = (param >> 4);
                    if ((param & 0x0F) > 0) ch_vibrato_depth[ch] = (param & 0x0F);
                    break;
            }

            // Pitch & Note Trigger Evaluation
            if (note > 0 && note <= 36) {
                uint16_t target_pitch = note_pitch_table[note - 1];
                if (eff == 0x03) {
                    // Legato mode: update target pitch without retriggering sample cursor
                    ch_target_pitch[ch] = target_pitch;
                } else {
                    memory[CH_PITCH(ch)] = (uint8_t)target_pitch;
                    memory[CH_TRIGGER(ch)] = 1;
                }
            }
        }
    } 
    // ─── TICKS 1+: CONTINUOUS RUNTIME MODIFIERS ───
    else {
        for (int ch = 0; ch < 4; ch++) {
            uint8_t eff   = row_ptr[(ch * 4) + 2];
            uint8_t param = row_ptr[(ch * 4) + 3];

            uint16_t cur_pitch = memory[CH_PITCH(ch)];
            uint16_t cur_vol   = memory[CH_VOLUME(ch)];

            switch (eff) {
                case 0x01: // Portamento Up
                    cur_pitch += param;
                    if (cur_pitch > 255) cur_pitch = 255;
                    break;
                    
                case 0x02: // Portamento Down
                    if (cur_pitch > param + 1) cur_pitch -= param;
                    else cur_pitch = 1; // Minimum pitch clamp
                    break;
                    
                case 0x03: // Tone Portamento (Slide towards target pitch)
                    if (cur_pitch < ch_target_pitch[ch]) {
                        cur_pitch += ch_porta_speed[ch];
                        if (cur_pitch > ch_target_pitch[ch]) cur_pitch = ch_target_pitch[ch];
                    } else if (cur_pitch > ch_target_pitch[ch]) {
                        if (cur_pitch > ch_porta_speed[ch]) cur_pitch -= ch_porta_speed[ch];
                        if (cur_pitch < ch_target_pitch[ch]) cur_pitch = ch_target_pitch[ch];
                    }
                    break;
                    
                case 0x04: // Vibrato (Pitch Oscillation)
                    ch_vibrato_phase[ch] += ch_vibrato_speed[ch];
                    int8_t vibrato_offset = ((ch_vibrato_phase[ch] & 0x04) ? 1 : -1) * ch_vibrato_depth[ch];
                    int16_t modulated_pitch = (int16_t)cur_pitch + vibrato_offset;
                    if (modulated_pitch < 1) modulated_pitch = 1;
                    if (modulated_pitch > 255) modulated_pitch = 255;
                    cur_pitch = (uint16_t)modulated_pitch;
                    break;
                    
                case 0x0A: // Volume Slide (High Nibble: Up, Low Nibble: Down)
                    if ((param >> 4) > 0) {
                        cur_vol += (param >> 4);
                        if (cur_vol > 255) cur_vol = 255;
                    } else if ((param & 0x0F) > 0) {
                        if (cur_vol >= (param & 0x0F)) cur_vol -= (param & 0x0F);
                        else cur_vol = 0;
                    }
                    break;
            }

            memory[CH_PITCH(ch)]  = (uint8_t)cur_pitch;
            memory[CH_VOLUME(ch)] = (uint8_t)cur_vol;
        }
    }

    // Advance tracker timing clock
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

// Audio Output Streaming Callback Loop
static void spu_callback(uint8_t *stream, int len) {
    uint8_t tracker_enabled = memory[TRACKER_ENABLED];

    for (int i = 0; i < len; i++) {
    
      if (cm_data && tracker_enabled == 1) {
            if (c_tracker_volume != c_fade_target) {
                c_tracker_volume += c_fade_step_per_sample;
                if ((c_fade_step_per_sample > 0.0f && c_tracker_volume >= c_fade_target) ||
                    (c_fade_step_per_sample < 0.0f && c_tracker_volume <= c_fade_target)) {
                    c_tracker_volume = c_fade_target;
                    c_fade_step_per_sample = 0.0f;
                }
                memory[TRACKER_VOLUME] = (uint8_t)(c_tracker_volume * 255.0f);
            } else {
                // Two-way sync: allow direct Lua memory writes to control volume
                c_tracker_volume = (float)memory[TRACKER_VOLUME] / 255.0f;
            }
        }
    
        tick_tracker();

        int32_t accum = 0;
        for (int ch = 0; ch < 4; ch++) {
            static uint8_t ch_persisted_mode[4] = {1, 1, 1, 1};

            if (memory[CH_TRIGGER(ch)] == 1) {
                ch_sample_cursor[ch] = 0;
                ch_brr_p1[ch] = 0;
                ch_brr_p2[ch] = 0;
                ch_current_block[ch] = -1; 
                
                // ─── FIX: Handle Manual Lua Trigger ADSR & Loop Initialization ───
                if (ch_adsr_state[ch] == ADSR_IDLE) {
                    ch_adsr_state[ch]  = ADSR_ATTACK;
                    ch_adsr_volume[ch] = 0; // Prevent loud initialization clicks

                    // Safe lookahead to setup whole-sample loop limits for manual play SFX
                    uint8_t status = (memory[CH_STATUS(ch)] == 0) ? ch_persisted_mode[ch] : memory[CH_STATUS(ch)];
                    uint32_t sample_len = ((uint32_t)memory[CH_LEN_HI(ch)] << 8) | memory[CH_LEN_LO(ch)];
                    
                    ch_loop_start_frame[ch] = 0;
                    ch_loop_end_frame[ch]   = (status == 2) ? (sample_len * 16) : sample_len;
                }
                
                memory[CH_TRIGGER(ch)] = 0;

                if (memory[CH_STATUS(ch)] == 0) {
                    memory[CH_STATUS(ch)] = ch_persisted_mode[ch];
                }
            }

            uint8_t status = memory[CH_STATUS(ch)];
            if (status == 0) continue; 

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

            // Playhead End Boundary Checks
            if (target_sample_idx >= total_samples) {
                if (looping) {
                    uint32_t l_start = ch_loop_start_frame[ch];
                    uint32_t l_end   = ch_loop_end_frame[ch];

                    if (l_end > l_start && l_end <= total_samples) {
                        ch_sample_cursor[ch] = l_start << 16;
                        target_sample_idx = l_start;
                    } else {
                        ch_sample_cursor[ch] = 0;
                        target_sample_idx = 0;
                    }

                    if (status == 2) {
                        ch_brr_p1[ch] = 0;
                        ch_brr_p2[ch] = 0;
                        ch_current_block[ch] = -1;
                    }
                } else { 
                    // No hardware loop checked -> naturally pass off to ADSR Release envelope
                    if (ch_adsr_state[ch] != ADSR_RELEASE && ch_adsr_state[ch] != ADSR_IDLE) {
                        ch_adsr_state[ch] = ADSR_RELEASE;
                    }
                    if (ch_adsr_state[ch] == ADSR_IDLE) {
                        memory[CH_STATUS(ch)] = 0; 
                        continue;
                    }
                }
            }
            
            /* ─── OPTIMIZED INTEGER SOFTWARE GATE RAMP ─── */
            uint32_t active_vol = vol;
            if (!looping) {
                if (target_sample_idx >= total_samples) {
                    // FIX: Force absolute silence if the playhead went past the end 
                    // while the ADSR Release envelope is still ramping down.
                    active_vol = 0; 
                } else {
                    uint32_t remaining = total_samples - target_sample_idx;
                    if (remaining <= GATE_RAMP_SAMPLES && total_samples > GATE_RAMP_SAMPLES) {
                        active_vol = (vol * remaining) / GATE_RAMP_SAMPLES;
                    }
                }
            }
            
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

            // ─── HARDWARE ENVELOPE AM PLITUDE STEP STATE MACHINE ───
            switch (ch_adsr_state[ch]) {
                case ADSR_IDLE:
                    ch_adsr_volume[ch] = 0;
                    break;
                case ADSR_ATTACK:
                    ch_adsr_volume[ch] += ch_adsr_attack[ch];
                    if (ch_adsr_volume[ch] >= 65536) {
                        ch_adsr_volume[ch] = 65536;
                        ch_adsr_state[ch] = ADSR_DECAY;
                    }
                    break;
                case ADSR_DECAY:
                    ch_adsr_volume[ch] -= ch_adsr_decay[ch];
                    if (ch_adsr_volume[ch] <= ch_adsr_sustain[ch]) {
                        ch_adsr_volume[ch] = ch_adsr_sustain[ch];
                        ch_adsr_state[ch] = ADSR_SUSTAIN;
                    }
                    break;
                case ADSR_SUSTAIN:
                    ch_adsr_volume[ch] = ch_adsr_sustain[ch];
                    break;
                case ADSR_RELEASE:
                    ch_adsr_volume[ch] -= ch_adsr_release[ch];
                    if (ch_adsr_volume[ch] <= 0) {
                        ch_adsr_volume[ch] = 0;
                        ch_adsr_state[ch] = ADSR_IDLE;
                        memory[CH_STATUS(ch)] = 0; // Cut voice execution entirely
                    }
                    break;
            }

            if (ch_adsr_state[ch] == ADSR_IDLE) continue;
            
            // 16-bit ADSR volume blending logic
            uint32_t envelope_scale = (ch_adsr_volume[ch] * active_vol) / 65536;
            signed_sample = (signed_sample * (int32_t)envelope_scale) / 255;
            accum += signed_sample;

            ch_sample_cursor[ch] += (pitch << 9);
        }

        if (tracker_enabled == 1) {
            accum = (accum * (int32_t)memory[TRACKER_VOLUME]) / 255;
        }

        int32_t mixed_output = 128 + accum;
        if (mixed_output > 255) mixed_output = 255;
        if (mixed_output < 0)   mixed_output = 0;

        stream[i] = (uint8_t)mixed_output;
        
        static uint8_t viz_write_ptr = 0;
        memory[ADDR_AUDIO_VIZ + viz_write_ptr] = (uint8_t)mixed_output;
        viz_write_ptr = (viz_write_ptr + 1) % 128;
        
        }
}


static void audio_stream_cb(float *buffer, int num_frames, int num_channels) {
    static uint8_t u8_buf[2048];
    int bytes = num_frames * num_channels;

    spu_callback(u8_buf, bytes);

    for (int i = 0; i < bytes; i++) {
        buffer[i] = (u8_buf[i] - 128) / 128.0f;
    }
}

void spu_init(void) {
    saudio_setup(&(saudio_desc){
        .sample_rate = 22050,
        .num_channels = 1,
        .stream_cb = audio_stream_cb,
        .logger.func = slog_func,
    });
}

void spu_shutdown(void) {
    saudio_shutdown();
}

void spu_start_module(const uint8_t* data, size_t size, float volume) {
    if (!data || size < 8) return;

    // Clean up previous module and make a safe copy of the raw binary blob
    if (cm_data) free(cm_data);
    cm_data = (uint8_t*)malloc(size);
    memcpy(cm_data, data, size);

    // Unpack header properties
    track_bpm   = cm_data[4];
    track_speed = cm_data[5];
    song_length = cm_data[6] | (cm_data[7] << 8);

    // Reset sequence playhead
    current_order       = 0;
    current_row         = 0;
    current_tick        = 0;
    sample_tick_counter = 0;

    // Configure initial sequence volume and enable tracker processing
    c_tracker_volume      = volume;
    c_fade_target         = volume;
    c_fade_step_per_sample = 0.0f;

    memory[TRACKER_VOLUME]  = (uint8_t)(volume * 255.0f);
    memory[TRACKER_ENABLED] = 1; 
}

static uint8_t ch_paused_status[4] = {0, 0, 0, 0};
static bool    tracker_paused = false;

void spu_pause_module(void) {
    if (tracker_paused || memory[TRACKER_ENABLED] == 0) return;

    for (int ch = 0; ch < 4; ch++) {
        ch_paused_status[ch] = memory[CH_STATUS(ch)];  
        memory[CH_STATUS(ch)] = 0;                       
    }
    memory[TRACKER_ENABLED] = 0;
    tracker_paused = true;
}

void spu_play_module(void) {
    if (!tracker_paused) return;
    for (int ch = 0; ch < 4; ch++) {
        memory[CH_STATUS(ch)] = ch_paused_status[ch];   
    }
    memory[TRACKER_ENABLED] = 1;
    tracker_paused = false;
}

void spu_fade_module(float target, int duration_frames) {
    c_fade_target = target;
    if (duration_frames <= 0) {
        c_tracker_volume = target;
        c_fade_step_per_sample = 0.0f;
        memory[TRACKER_VOLUME] = (uint8_t)(target * 255.0f);
    } else {
        // 22050 Hz / 60 FPS = 367.5 samples per frame
        float total_samples = (float)duration_frames * 367.5f;
        c_fade_step_per_sample = (target - c_tracker_volume) / total_samples;
    }
}

void spu_stop_module(void) {
    memory[TRACKER_ENABLED] = 0;
    for (int ch = 0; ch < 4; ch++) {
        memory[CH_STATUS(ch)] = 0;
        ch_sample_cursor[ch] = 0;
        ch_adsr_state[ch] = ADSR_IDLE;
        ch_adsr_volume[ch] = 0;
    }
    current_order = 0;
    current_row = 0;
    current_tick = 0;
}
