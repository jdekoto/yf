// main.c
#define KIT_IMPL
#include "kit.h"
#include "mem.h"
#include "vm.h"
#include "audio.h"
#include "font.h"
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

void fb_expand(Pixel *dst) {
    uint8_t  *fb  = memory + ADDR_FB;
    uint32_t *pal = (uint32_t *)(memory + ADDR_PAL);
    for (int i = 0; i < FB_WID * FB_HEI; i++)
        dst[i] = pal[fb[i] & 0x0f];
}

void map_inputs(kit_Context *ctx) {
    // 1. Shift BOTH current bytes (40, 41) into the previous slots (42, 43)
    poke(0x03042, peek(0x03040));
    poke(0x03043, peek(0x03041));

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
    poke(0x03040, (uint8_t)(mask & 0xFF));        // Lower byte (bits 0-7)
    poke(0x03041, (uint8_t)((mask >> 8) & 0xFF)); // Upper byte (bits 8-15)
}

// loads everything needed into ram.
static void mem_init() {
    // initialize the array
    memset(memory, 0, RAM_SIZE);
    
    // palette from ShrimpCatDev/CherryPop on github
    const uint32_t default_palette[16] = {
      RGBA(0x17, 0x19, 0x1b, 0xff), // asphalt
      RGBA(0x28, 0x23, 0x7b, 0xff), // ocean  
      RGBA(0x32, 0x59, 0xe2, 0xff), // blue sky
      RGBA(0x33, 0xa5, 0xff, 0xff), // neon 
      RGBA(0x0a, 0x4b, 0x4d, 0xff), // rainforest
      RGBA(0x72, 0xcb, 0x25, 0xff), // bamboo
      RGBA(0xff, 0xc4, 0x38, 0xff), // solar
      RGBA(0xf0, 0x6c, 0x00, 0xff), // tangerine
      RGBA(0xd1, 0x28, 0x41, 0xff), // strawberry
      RGBA(0x57, 0x14, 0x2e, 0xff), // cherry
      RGBA(0x97, 0x3f, 0x3f, 0xff), // healthy soil
      RGBA(0xf1, 0xc2, 0x84, 0xff), // caucasian
      RGBA(0xe5, 0x5d, 0xac, 0xff), // bubblegum
      RGBA(0xf1, 0xf0, 0xee, 0xff), // white
      RGBA(0x96, 0xa5, 0xab, 0xff), // cobblestone
      RGBA(0x58, 0x6c, 0x79, 0xff), // concrete
    };
    
    for (int i = 0; i < 16; i++) {
        uint32_t current_color_address = ADDR_PAL + (i * 4);
        poke4(current_color_address, default_palette[i]);
    }
    
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
    const char *target_folder = argv[1];

    // 1. Move the engine's current working directory directly INTO the target folder!
    // This instantly makes the cartridge folder the "root" for the game.
    if (chdir(target_folder) != 0) {
        printf("ERROR: Could not open or find cartridge folder: %s\n", target_folder);
        return 1;
    }
    
    mem_init();
    spu_init();
    vm_init(&vm);
    
    kit_Context *ctx = kit_create("yf", FB_WID, FB_HEI, KIT_SCALE4X);
    config_title(ctx->window, "config.txt");
    double dt;
    while (kit_step(ctx, &dt)) {
    
        vm_reload(&vm, "boot.lua");
        map_inputs(ctx);
        fb_expand(framebuf); 
        vm_update(&vm);
    
    }

    vm_shutdown(&vm);
    kit_destroy(ctx);
    return 0;
}
