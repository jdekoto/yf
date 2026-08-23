// audio.c
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include "audio.h"

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
static float c_fade_step_per_chunk = 0.0f;

// Frequency lookup map: 128 represents 1.0 standard baseline playback speed
static const uint8_t note_pitch_table[36] = {
    16, 17, 18, 19, 20, 21, 23, 24, 25, 27, 29, 30,
    32, 34, 36, 38, 40, 43, 45, 48, 51, 54, 57, 60,
    128, 136, 144, 152, 161, 171, 181, 192, 203, 215, 228, 242
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

// Sequencer routing clock
void tick_tracker(void) {
    if (!cm_data || memory[TRACKER_ENABLED] == 0) return;

    uint32_t samples_per_tick = (22050 * 5) / (track_bpm * 2);
    sample_tick_counter++;
    
    if (sample_tick_counter < samples_per_tick) return;
    sample_tick_counter = 0;

    // ─── TICK 0: ROW EVALUATION (TRIGGER ENVELOPES / SAMPLES) ───
    if (current_tick == 0) {
        uint8_t* order_list = cm_data + 8;
        uint8_t pattern_idx = order_list[current_order];
        uint8_t* pattern_ptr = cm_data + 8 + 128 + (pattern_idx * 1024);
        uint8_t* row_ptr = pattern_ptr + (current_row * 16);
        
        memory[ADDR_AUDIO + 0x48] = current_order;
        memory[ADDR_AUDIO + 0x49] = current_row;
        memory[ADDR_AUDIO + 0x4A] = pattern_idx;

        for (int ch = 0; ch < 4; ch++) {
            uint8_t note  = row_ptr[(ch * 4) + 0];
            uint8_t inst  = row_ptr[(ch * 4) + 1];
            uint8_t eff   = row_ptr[(ch * 4) + 2];
            uint8_t param = row_ptr[(ch * 4) + 3];

            // 1. Unpack Aligned 12-Byte Asset Layout
            if (inst > 0 && inst <= 64) {
                uint32_t slot = inst - 1;
                uint32_t header_ptr = ADDR_SNDBNK + (slot * 12); // Updated 12-byte leap boundary
                
                uint32_t sample_offset = *(uint32_t*)&memory[header_ptr + 0];
                uint32_t sample_length = *(uint32_t*)&memory[header_ptr + 4];
                uint16_t loop_start    = *(uint16_t*)&memory[header_ptr + 8];
                uint8_t  def_volume    = memory[header_ptr + 10];
                uint8_t  flags         = memory[header_ptr + 11];

                // Synchronize raw structural markers into SPU Channel Config space
                memory[CH_ADDR_LO(ch)] = (uint8_t)(sample_offset & 0xFF);
                memory[CH_ADDR_HI(ch)] = (uint8_t)((sample_offset >> 8) & 0xFF);
                memory[CH_LEN_LO(ch)]  = (uint8_t)(sample_length & 0xFF);
                memory[CH_LEN_HI(ch)]  = (uint8_t)((sample_length >> 8) & 0xFF);
                memory[CH_VOLUME(ch)]  = def_volume;
                memory[CH_LOOP(ch)]    = flags & 0x01;
                memory[CH_STATUS(ch)]  = (flags & 0x02) ? 2 : 1;

                // Cache calculated playhead loop parameters locally
                ch_loop_start_frame[ch] = (uint32_t)loop_start;
                ch_loop_end_frame[ch]   = (flags & 0x02) ? (sample_length * 16) : sample_length;

                // Kickstart the Envelope State Matrix
                ch_adsr_state[ch]  = ADSR_ATTACK;
                ch_adsr_volume[ch] = 0; // Guard against popping initialization clicks
            }
            
            // 2. Note-Off / Key-Off Detection Rules (Note 0xFF or custom tracker overrides)
            if (note == 0xFF) {
                ch_adsr_state[ch] = ADSR_RELEASE;
            }

            if (eff == 0x0C) { /* Set Volume */
                memory[CH_VOLUME(ch)] = param;
            }

            // 3. Runtime ADSR Live Envelope Modification Effects
            if (eff == 0x08) { // Effect 0x08: Set Attack [High Nibble] & Release [Low Nibble] Rates
                if ((param >> 4) > 0)   ch_adsr_attack[ch]  = (param >> 4) * 150;
                if ((param & 0x0F) > 0) ch_adsr_release[ch] = (param & 0x0F) * 50;
            }
            if (eff == 0x09) { // Effect 0x09: Set Sustain Level
                ch_adsr_sustain[ch] = (int32_t)param * 257; // Maps 0-255 scaling straight up to 0-65536
            }

            if (note > 0 && note <= 36) {
                uint16_t target_pitch = note_pitch_table[note - 1];
                if (eff == 0x03) {
                    ch_target_pitch[ch] = target_pitch;
                } else {
                    memory[CH_PITCH(ch)] = (uint8_t)target_pitch;
                    memory[CH_TRIGGER(ch)] = 1;
                }
            }

            if (eff == 0x04) {
                if ((param >> 4) > 0) ch_vibrato_speed[ch] = (param >> 4);
                if ((param & 0x0F) > 0) ch_vibrato_depth[ch] = (param & 0x0F);
            }
        }
    } 
    // ─── TICKS 1+: RUNTIME MODIFIERS ───
    else {
        uint8_t* order_list = cm_data + 8;
        uint8_t pattern_idx = order_list[current_order];
        uint8_t* pattern_ptr = cm_data + 8 + 128 + (pattern_idx * 1024);
        uint8_t* row_ptr = pattern_ptr + (current_row * 16);

        for (int ch = 0; ch < 4; ch++) {
            uint8_t eff   = row_ptr[(ch * 4) + 2];
            uint8_t param = row_ptr[(ch * 4) + 3];

            uint16_t cur_pitch = memory[CH_PITCH(ch)];
            uint8_t  cur_vol   = memory[CH_VOLUME(ch)];

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

// Audio Output Streaming Callback Loop
static void spu_callback(uint8_t *stream, int len) {
    uint8_t tracker_enabled = memory[TRACKER_ENABLED];

    if (cm_data && tracker_enabled == 1) {
        if (c_tracker_volume != c_fade_target) {
            c_tracker_volume += c_fade_step_per_chunk;
            if ((c_fade_step_per_chunk > 0.0f && c_tracker_volume >= c_fade_target) ||
                (c_fade_step_per_chunk < 0.0f && c_tracker_volume <= c_fade_target)) {
                c_tracker_volume = c_fade_target;
                c_fade_step_per_chunk = 0.0f;
            }
            memory[TRACKER_VOLUME] = (uint8_t)(c_tracker_volume * 255.0f);
        }
    }

    for (int i = 0; i < len; i++) {
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

void spu_start_module(const char* filename, float volume) {
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

    memory[TRACKER_VOLUME] = (uint8_t)(volume * 255.0f);
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
        c_fade_step_per_chunk = 0.0f;
        memory[TRACKER_VOLUME] = (uint8_t)(target * 255.0f);
    } else {
        float total_chunks = (float)duration_frames * (86.1328f / 60.0f);
        c_fade_step_per_chunk = (target - c_tracker_volume) / total_chunks;
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
