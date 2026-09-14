#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "headers/stb_image_write.h"

#define MSF_GIF_IMPL
#include "headers/msf_gif.h"
#include <time.h>
#include "headers/snap.h"

// --- MEDIA CAPTURE STATE ---
static bool is_recording_gif = false;
static MsfGifState gif_state = {0};

// Helper: Converts your 16-bit RGB565 framebuf into 32-bit RGBA for GIF export
static void get_framebuf_rgba(uint8_t *rgba_out) {
    for (int i = 0; i < FB_WID * FB_HEI; i++) {
        uint16_t c = framebuf[i];
        
        // Unpack RGB565 to RGBA8888
        uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
        uint8_t g = ((c >> 5)  & 0x3F) * 255 / 63;
        uint8_t b = (c         & 0x1F) * 255 / 31;

        rgba_out[i * 4 + 0] = r;
        rgba_out[i * 4 + 1] = g;
        rgba_out[i * 4 + 2] = b;
        rgba_out[i * 4 + 3] = 255; // Full opacity
    }
}

// Zero-dependency 24-bit BMP screenshot writer
void screenshot(void) {
    char filename[64];
    snprintf(filename, sizeof(filename), "screenshot_%unsigned.bmp", (unsigned int)time(NULL));

    FILE *f = fopen(filename, "wb");
    if (!f) return;

    int pad = (4 - (FB_WID * 3) % 4) % 4;
    uint32_t filesize = 54 + (FB_WID * 3 + pad) * FB_HEI;

    uint8_t header[54] = {
        'B','M', filesize, filesize>>8, filesize>>16, filesize>>24,
        0,0, 0,0, 54,0,0,0, 40,0,0,0,
        FB_WID, FB_WID>>8, FB_WID>>16, FB_WID>>24,
        FB_HEI, FB_HEI>>8, FB_HEI>>16, FB_HEI>>24,
        1,0, 24,0
    };

    fwrite(header, 1, 54, f);

    // Write pixel array bottom-to-top as required by BMP spec
    for (int y = FB_HEI - 1; y >= 0; y--) {
        for (int x = 0; x < FB_WID; x++) {
            uint16_t c = framebuf[y * FB_WID + x];
            uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
            uint8_t g = ((c >> 5)  & 0x3F) * 255 / 63;
            uint8_t b = (c         & 0x1F) * 255 / 31;
            uint8_t bgr[3] = { b, g, r };
            fwrite(bgr, 1, 3, f);
        }
        uint8_t padding[3] = {0, 0, 0};
        if (pad > 0) fwrite(padding, 1, pad, f);
    }

    fclose(f);
    printf("[ENGINE] Saved screenshot to %s\n", filename);
}

// GIF Recording Toggle (F9 Key)
void toggle_gif(void) {
    if (!is_recording_gif) {
        if (msf_gif_begin(&gif_state, FB_WID, FB_HEI)) {
            is_recording_gif = true;
            printf("[GIF] Started recording...\n");
        }
    } else {
        is_recording_gif = false;
        MsfGifResult result = msf_gif_end(&gif_state);

        if (result.data) {
            char filename[64];
            snprintf(filename, sizeof(filename), "clip_%unsigned.gif", (unsigned int)time(NULL));
            FILE *fp = fopen(filename, "wb");
            if (fp) {
                fwrite(result.data, result.dataSize, 1, fp);
                fclose(fp);
                printf("[GIF] Saved animation to %s\n", filename);
            }
            msf_gif_free(result);
        }
    }
}

// Call once per frame inside your main loop when active
void gif_tick(void) {
    if (!is_recording_gif) return;

    static uint8_t rgba[FB_WID * FB_HEI * 4];
    get_framebuf_rgba(rgba);

    // 2 centiseconds = 20ms delay (~50-60 FPS timing), Quality = 16 (default)
    msf_gif_frame(&gif_state, rgba, 2, 16, FB_WID * 4);
}
