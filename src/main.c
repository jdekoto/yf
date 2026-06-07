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
static bool is_yfc = false;
static char game_title[32];

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

static void title_handler(const char *path, bool is_cart, long offset) {
    if (is_cart) {
        // --- CARTRIDGE BINARY EXTRACTOR ---
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            lseek(fd, offset, SEEK_SET);
            char magic[4];
            if (read(fd, magic, 4) == 4 && strncmp(magic, "YFC!", 4) == 0) {
                // The title is stored immediately after the 4-byte magic token for 32 bytes
                char raw_title[32] = {0};
                if (read(fd, raw_title, 32) == 32) {
                    // Force a safe null-terminator in case the header was un-terminated
                    raw_title[31] = '\0'; 
                    // If it wasn't left completely empty, copy it to our engine runtime variable
                    if (strlen(raw_title) > 0) {
                        strncpy(game_title, raw_title, 32);
                    }
                }
            }
            close(fd);
        }
    } else {
        // --- LOCAL DIRECTORY CONFIG EXTRACTOR ---
        FILE *cf = fopen("config.txt", "rb");
        if (!cf) {
            // Backup fallback check if we haven't chdir'd yet
            char alt_path[512];
            snprintf(alt_path, sizeof(alt_path), "%s/config.txt", path);
            cf = fopen(alt_path, "rb");
        }

        if (cf) {
            fseek(cf, 0, SEEK_END);
            long size = ftell(cf);
            fseek(cf, 0, SEEK_SET);
            
            char *buf = malloc(size + 1);
            if (buf) {
                size_t read_bytes = fread(buf, 1, size, cf);
                buf[read_bytes] = '\0';
                
                char extracted_title[32] = {0};
                // Utilize your existing configuration string parser!
                parse_config(buf, "title", extracted_title, sizeof(extracted_title));
                
                if (strlen(extracted_title) > 0) {
                    strncpy(game_title, extracted_title, 32);
                }
                free(buf);
            }
            fclose(cf);
        }
    }
}

static long find_sentinel(int fd, long file_size) {
    const char sentinel[8] = {
        0xDE, 0xAD, 0xBE, 0xEF,
        0xCA, 0xFE, 0xBA, 0xBE
    };

    lseek(fd, 0, SEEK_SET);
    char buf[4096];
    long pos = 0;
    ssize_t n;
    long found = -1;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n - 8; i++) {
            if (memcmp(buf + i, sentinel, 8) == 0)
                found = pos + i + 8;  /* byte AFTER sentinel = exe end */
        }
        pos += n;
    }
    return found;   /* -1 if not found */
}

static long find_appended(const char *exe_path) {
    int fd = open(exe_path, O_RDONLY);
    if (fd < 0) return -1;

    long file_size = lseek(fd, 0, SEEK_END);
    if (file_size < 1024) { close(fd); return -1; }

    /* find where exe code ends */
    long exe_end = find_sentinel(fd, file_size);
    if (exe_end < 0) { close(fd); return -1; }  /* no sentinel = not a fused binary */

    const char sig[4] = {'Y', 'F', 'C', '!'};
    lseek(fd, exe_end, SEEK_SET);  /* only search from exe_end onwards */

    char buf[4096];
    long pos = exe_end;
    ssize_t n;
    long found_offset = -1;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n - 4; i++) {
            if (buf[i]   == sig[0] && buf[i+1] == sig[1] &&
                buf[i+2] == sig[2] && buf[i+3] == sig[3]) {
                found_offset = pos + i;
            }
        }
        pos += n;
    }
    close(fd);
    return found_offset;
}

int main(int argc, char *argv[]) {
    
    // initiate the system before argument handling
    mem_init();
    spu_init();
    vm_init(&vm);
    
    // first are we fused?
    long fused_offset = find_appended(argv[0]);
    
    if (fused_offset >= 0) {
        printf("[ENGINE] Fused game stream payload identified at byte offset: %ld\n", fused_offset);
        is_yfc = true;
        
        // We pass the engine's own running path as the cartridge target argument!
        title_handler(argv[0], true, fused_offset);
        yfc_boot(&vm, argv[0], fused_offset);
        
        goto launch_window;
    }

    if (argc < 2) {
        vm_bios(&vm);
        goto launch_window;
    }
    
    if (strcmp(argv[1], "--help") == 0) {
        printf("Usage:\n"
        "To run a folder:    ./yf <cassette_folder>\n"
        "To pack a cart:    ./yf --package <cassette_folder> <cassette_name>\n"
        );
        
        return 1;
    }

    // --- HANDLE PACKAGING ARGUMENT ---
    
    const char *target = argv[1];
    
    if (strcmp(argv[1], "--package") == 0) {
        if (argc < 3) {
            printf("Error: Please specify a folder to package.\n");
            return 1;
        }
        printf("Packaging %s into a standalone .yfc cartridge...\n", argv[2]);
        yfc_pack(argv[2], argv[3]);
        return 0;
    }
    
    // --- RUNNING A CARTRIDGE FOLDER ---
    if (has_extension(target, ".yfc")) {
        is_yfc = true;
        printf("WARNING: Cassette format not tested to its fullest extent\n");
        title_handler(target, true, 0);
        yfc_boot(&vm, target, 0);

     } else {
        is_yfc = false;
        if (chdir(target) != 0) {
            printf("ERROR: Could not open or find cartridge folder: %s\n", target);
            return 1;
        }
        title_handler(target, false, 0);
     }
     
    launch_window:
    
    kit_Context *ctx = kit_create(game_title, FB_WID, FB_HEI, KIT_SCALE4X);
    double dt;
    while (kit_step(ctx, &dt)) {
    
        if (!is_yfc) { vm_reload(&vm, "boot.lua"); }
        map_inputs(ctx);
        fb_expand(framebuf); 
        vm_update(&vm);
    
    }
    if (is_yfc) {
        char cleanup_cmd[256];
        // Targets the exact process ID used during the boot process
        snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rm -rf /tmp/yf_sandbox_%d", getpid());
        system(cleanup_cmd);
    }
    vm_shutdown(&vm);
    kit_destroy(ctx);
    return 0;
}

const char end_of_executable[8] = {
    0xDE, 0xAD, 0xBE, 0xEF,
    0xCA, 0xFE, 0xBA, 0xBE
};
