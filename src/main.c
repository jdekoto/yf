// main.c
#define KIT_IMPL
#include "kit.h"
#include "mem.h"
#include "vm.h"
#include "audio.h"
#include "font.h"
#include "yfc.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
    #include <direct.h>
    #define chdir _chdir
#else
    #include <unistd.h>
#endif

static VM vm;
static bool is_yfc_cartridge = false;

void fb_expand(uint16_t *dst) {
    // Point to the beginning of your 16-bit Framebuffer in RAM
    uint8_t *fb = (uint8_t*)memory + ADDR_FB;
    
    for (int i = 0; i < FB_WID * FB_HEI; i++) {
        // Because each pixel is now 2 bytes, calculate the byte index
        int byte_idx = i * 2;
        
        // Grab the Low Byte and High Byte from your flat RAM array
        uint8_t low  = fb[byte_idx];
        uint8_t high = fb[byte_idx + 1];
        
        // Combine them back into a single 16-bit color integer
        uint16_t color16 = low | (high << 8);
        
        // Write it directly to the SDL texture / destination pixel array!
        dst[i] = color16;
    }
}

void map_inputs(kit_Context *ctx) {
    // 1. Shift BOTH current bytes (40, 41) into the previous slots (42, 43)
    poke(0x06042, peek(0x06040));
    poke(0x06043, peek(0x06041));

    // 2. Use a 16-bit integer so Bit 8 (Enter) doesn't get chopped off!
    uint16_t mask = 0;
    
    if (kit_key_down(ctx, SDL_SCANCODE_LEFT))    mask |= (1 << 0);
    if (kit_key_down(ctx, SDL_SCANCODE_RIGHT))   mask |= (1 << 1);
    if (kit_key_down(ctx, SDL_SCANCODE_UP))      mask |= (1 << 2);
    if (kit_key_down(ctx, SDL_SCANCODE_DOWN))    mask |= (1 << 3);
    if (kit_key_down(ctx, SDL_SCANCODE_A))       mask |= (1 << 4); // Button A
    if (kit_key_down(ctx, SDL_SCANCODE_S))       mask |= (1 << 5); // Button B
    if (kit_key_down(ctx, SDL_SCANCODE_Z))       mask |= (1 << 6); // Button C
    if (kit_key_down(ctx, SDL_SCANCODE_X))       mask |= (1 << 7); // Button D
    if (kit_key_down(ctx, SDL_SCANCODE_RETURN))  mask |= (1 << 8); // Enter / Start

    // 3. Poke the 16-bit mask across our two back-to-back registers
    poke(0x06040, (uint8_t)(mask & 0xFF));        // Lower byte (bits 0-7)
    poke(0x06041, (uint8_t)((mask >> 8) & 0xFF)); // Upper byte (bits 8-15)
}

// loads everything needed into ram.
static void mem_init() {
    // initialize the array
    memset(memory, 0, RAM_SIZE);
    
    kit_Image *font_img = NULL;
    
    font_img = kit_load_image_mem(cp_font_png, cp_font_png_len);
    if (!font_img) return;
    
    int num_chars = font_img->w / 5; 

    if (num_chars > 90) num_chars = 90;

    for (int char_idx = 0; char_idx < num_chars; char_idx++) {
        uint32_t char_base_addr = ADDR_FONT + (char_idx * 6);
        for (int row = 0; row < 5; row++) {
            uint8_t row_mask = 0;

            for (int col = 0; col < 5; col++) {

                int img_x = (char_idx * 5) + col;
                int img_y = row;

                kit_Color px = font_img->pixels[img_y * font_img->w + img_x];

                if (px.r > 128) {
                    row_mask |= (0x80 >> col);
                }
            }
            poke(char_base_addr + row, row_mask);
        }
        poke(char_base_addr + 5, 0x00);
    }
}

// Helper: Safely looks up if a string matches a specific file extension
static bool has_extension(const char *filename, const char *ext) {
    size_t len = strlen(filename);
    size_t ext_len = strlen(ext);
    return len > ext_len && strcmp(filename + len - ext_len, ext) == 0;
}

int main(int argc, char *argv[]) {

    if (argc < 2) {
        printf("Usage:\n  To run a folder:    ./yf <cartridge_folder>\n  To pack a cart:    ./yf --package <cartridge_folder>\n");
        return 1;
    }

    // --- HANDLE PACKAGING ARGUMENT ---
    if (strcmp(argv[1], "--package") == 0) {
        if (argc < 3) {
            printf("Error: Please specify a folder to package.\n");
            return 1;
        }
        printf("Packaging %s into a standalone .yfc cartridge...\n", argv[2]);
        printf("Nah i'm kidding its not in yet\n");
        // (We will hook our packager algorithm here!)
        return 0;
    }
    // --- RUNNING A CARTRIDGE FOLDER ---
    const char *target = argv[1];

    if (has_extension(target, ".yfc")) {
        is_yfc_cartridge = true;
        printf("ERROR: not fully implemented nor working\n");
        //boot_yfc(&vm, target);
     } else {
        is_yfc_cartridge = false;
        if (chdir(target) != 0) {
            printf("ERROR: Could not open or find cartridge folder: %s\n", target);
            return 1;
        }
     }
    
    mem_init();
    spu_init();
    vm_init(&vm);
    
    kit_Context *ctx = kit_create("yf", FB_WID, FB_HEI, KIT_SCALE4X);
    config_title(ctx->window, "config.txt");
    double dt;
    while (kit_step(ctx, &dt)) {
    
        if (!is_yfc_cartridge) {
            vm_reload(&vm, "boot.lua");
        }
        map_inputs(ctx);
        fb_expand(framebuf); 
        vm_update(&vm);
    
    }

    vm_shutdown(&vm);
    kit_destroy(ctx);
    return 0;
}
