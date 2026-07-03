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
#include <limits.h>


#ifndef O_BINARY
#define O_BINARY 0   /* linux/mac don't need it, harmless to define */
#endif

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define chdir _chdir
    #define getpid _getpid
    // Force mkdir to drop the mode argument on Windows
    #define mkdir(path) _mkdir(path) 
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #define mkdir(path) mkdir(path, 0755)
#endif

static VM vm;
static bool is_yfc = false;
static char game_title[32];
static char game_id[8];

void fb_expand(uint16_t *dst) {
    // Point to the beginning of your 16-bit Framebuffer in RAM
    uint8_t *fb = (uint8_t*)memory + ADDR_FB;
    
    // Grab the wave parameters from your hardware registers
    uint8_t amp  = memory[REG_WAVE_AMP];
    uint8_t freq = memory[REG_WAVE_FREQ];
    uint8_t t    = memory[REG_WAVE_TIME];

    // Loop through every scanline (Y)
    for (int y = 0; y < FB_HEI; y++) {
        int h_offset = 0;
        
        // Calculate the wave offset ONCE per row
        if (amp > 0) {
            h_offset = (int)(sinf((y * freq + t) * 0.05f) * amp);
        }

        // Loop through every pixel in this scanline (X)
        for (int x = 0; x < FB_WID; x++) {
            // 1. Compute where this pixel lands on the screen texture
            int dst_idx = (y * FB_WID) + x;

            // 2. Apply the horizontal shift to our source X coordinate, wrapping edges
            int src_x = (x + h_offset) % FB_WID;
            if (src_x < 0) src_x += FB_WID; // Handle negative wrapping safely

            // 3. Convert the shifted (src_x, y) back into a flat 1D pixel index
            int src_pixel_idx = (y * FB_WID) + src_x;
            
            // 4. Because each pixel is 2 bytes, calculate the byte index in RAM
            int byte_idx = src_pixel_idx * 2;
            
            // 5. Grab the Low Byte and High Byte from your flat RAM array (Your exact logic!)
            uint8_t low  = fb[byte_idx];
            uint8_t high = fb[byte_idx + 1];
            
            // 6. Combine them back into a single 16-bit color integer
            uint16_t color16 = low | (high << 8);
            
            // 7. Write it directly to the SDL texture / destination pixel array!
            dst[dst_idx] = color16;
        }
    }
}

void map_inputs(kit_Context *ctx) {
    // Refresh controller connection states
    
    poke(0x06444, peek(0x06440));
    poke(0x06445, peek(0x06441));
    poke(0x06446, peek(0x06442));
    poke(0x06447, peek(0x06443));

    uint32_t final_mask = 0;

    // --- PLAYER 1 SUB-MASK MAPPING (Bits 0-8) ---
    uint16_t p1_mask = 0;
    if (kit_key_down(ctx, SDL_SCANCODE_LEFT))    p1_mask |= (1 << 0);
    if (kit_key_down(ctx, SDL_SCANCODE_RIGHT))   p1_mask |= (1 << 1);
    if (kit_key_down(ctx, SDL_SCANCODE_UP))      p1_mask |= (1 << 2);
    if (kit_key_down(ctx, SDL_SCANCODE_DOWN))    p1_mask |= (1 << 3);
    if (kit_key_down(ctx, SDL_SCANCODE_A))       p1_mask |= (1 << 4); 
    if (kit_key_down(ctx, SDL_SCANCODE_S))       p1_mask |= (1 << 5); 
    if (kit_key_down(ctx, SDL_SCANCODE_Z))       p1_mask |= (1 << 6); 
    if (kit_key_down(ctx, SDL_SCANCODE_X))       p1_mask |= (1 << 7); 
    if (kit_key_down(ctx, SDL_SCANCODE_RETURN))  p1_mask |= (1 << 8);

    if (ctx->pad1) {
        if (SDL_GameControllerGetButton(ctx->pad1, SDL_CONTROLLER_BUTTON_DPAD_LEFT))   p1_mask |= (1 << 0);
        if (SDL_GameControllerGetButton(ctx->pad1, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))  p1_mask |= (1 << 1);
        if (SDL_GameControllerGetButton(ctx->pad1, SDL_CONTROLLER_BUTTON_DPAD_UP))     p1_mask |= (1 << 2);
        if (SDL_GameControllerGetButton(ctx->pad1, SDL_CONTROLLER_BUTTON_DPAD_DOWN))   p1_mask |= (1 << 3);
        if (SDL_GameControllerGetButton(ctx->pad1, SDL_CONTROLLER_BUTTON_A))           p1_mask |= (1 << 4);
        if (SDL_GameControllerGetButton(ctx->pad1, SDL_CONTROLLER_BUTTON_B))           p1_mask |= (1 << 5);
        if (SDL_GameControllerGetButton(ctx->pad1, SDL_CONTROLLER_BUTTON_X))           p1_mask |= (1 << 6);
        if (SDL_GameControllerGetButton(ctx->pad1, SDL_CONTROLLER_BUTTON_Y))           p1_mask |= (1 << 7);
        if (SDL_GameControllerGetButton(ctx->pad1, SDL_CONTROLLER_BUTTON_START))       p1_mask |= (1 << 8);

        // Optional: Left Analog D-Pad deadzone fallback overrides
        int16_t ax = SDL_GameControllerGetAxis(ctx->pad1, SDL_CONTROLLER_AXIS_LEFTX);
        int16_t ay = SDL_GameControllerGetAxis(ctx->pad1, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax < -16000) p1_mask |= (1 << 0);
        if (ax >  16000) p1_mask |= (1 << 1);
        if (ay < -16000) p1_mask |= (1 << 2);
        if (ay >  16000) p1_mask |= (1 << 3);
    }
    final_mask |= p1_mask;

    // --- PLAYER 2 SUB-MASK MAPPING (Bits 0-8 local, then shifted up) ---
    uint16_t p2_mask = 0;
    // as of right now, you will need two controllers to do two player, needa figure out kbd mappings
    
    if (ctx->pad2) {
        if (SDL_GameControllerGetButton(ctx->pad2, SDL_CONTROLLER_BUTTON_DPAD_LEFT))   p2_mask |= (1 << 0);
        if (SDL_GameControllerGetButton(ctx->pad2, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))  p2_mask |= (1 << 1);
        if (SDL_GameControllerGetButton(ctx->pad2, SDL_CONTROLLER_BUTTON_DPAD_UP))     p2_mask |= (1 << 2);
        if (SDL_GameControllerGetButton(ctx->pad2, SDL_CONTROLLER_BUTTON_DPAD_DOWN))   p2_mask |= (1 << 3);
        if (SDL_GameControllerGetButton(ctx->pad2, SDL_CONTROLLER_BUTTON_A))           p2_mask |= (1 << 4);
        if (SDL_GameControllerGetButton(ctx->pad2, SDL_CONTROLLER_BUTTON_B))           p2_mask |= (1 << 5);
        if (SDL_GameControllerGetButton(ctx->pad2, SDL_CONTROLLER_BUTTON_X))           p2_mask |= (1 << 6);
        if (SDL_GameControllerGetButton(ctx->pad2, SDL_CONTROLLER_BUTTON_Y))           p2_mask |= (1 << 7);
        if (SDL_GameControllerGetButton(ctx->pad2, SDL_CONTROLLER_BUTTON_START))       p2_mask |= (1 << 8);

        int16_t ax = SDL_GameControllerGetAxis(ctx->pad2, SDL_CONTROLLER_AXIS_LEFTX);
        int16_t ay = SDL_GameControllerGetAxis(ctx->pad2, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax < -16000) p2_mask |= (1 << 0);
        if (ax >  16000) p2_mask |= (1 << 1);
        if (ay < -16000) p2_mask |= (1 << 2);
        if (ay >  16000) p2_mask |= (1 << 3);
    }
    
    // Shift Player 2 inputs up precisely past Player 1's Start key slot
    final_mask |= ((uint32_t)p2_mask << 9);

    // 3. Poke the 32-bit aggregated mask cleanly across the 4 sequential bytes
    poke(0x06440, (uint8_t)(final_mask & 0xFF));
    poke(0x06441, (uint8_t)((final_mask >> 8) & 0xFF));
    poke(0x06442, (uint8_t)((final_mask >> 16) & 0xFF));
    poke(0x06443, (uint8_t)((final_mask >> 24) & 0xFF));
}

// --- AUTOMATED ENGINE SRAM PERSISTENCE LAYER ---
#ifdef _WIN32
    #define PATH_SEP_STR "\\"
#elif defined(__APPLE__)
    #include <mach-o/dyld.h> // Required for macOS executable path tracking
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <limits.h>
    #define PATH_SEP_STR "/"
#else
    #define PATH_SEP_STR "/"
#endif

static void get_saves_directory(char *out_dir, size_t max_size) {
#ifdef _WIN32
    char buffer[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    char *last_slash = strrchr(buffer, '\\');
    if (last_slash) *last_slash = '\0';
    snprintf(out_dir, max_size, "%s" PATH_SEP_STR "saves", buffer);
#elif defined(__APPLE__)
    char buffer[PATH_MAX] = {0};
    uint32_t size = sizeof(buffer);
    
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        // Resolve any symlinks to get the absolute, clean path
        char real_res[PATH_MAX];
        if (realpath(buffer, real_res) != NULL) {
            strncpy(buffer, real_res, sizeof(buffer) - 1);
        }

        // 1. Strip the executable name (e.g., /yf → Contents/MacOS)
        char *sl = strrchr(buffer, '/');
        if (sl) {
            *sl = '\0';
            
            // 2. Strip the MacOS directory (e.g., /MacOS → Contents)
            sl = strrchr(buffer, '/');
            if (sl) {
                *sl = '\0';
                
                // 3. Build the final path targeted inside Resources/saves
                snprintf(out_dir, max_size, "%s/Resources/saves", buffer);
                return;
            }
        }
    }
    // Fallback if running outside of a standard macOS App Bundle structure
    snprintf(out_dir, max_size, "." PATH_SEP_STR "saves");
#else // Linux
    char buffer[PATH_MAX] = {0};
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        char *last_slash = strrchr(buffer, '/');
        if (last_slash) *last_slash = '\0';
        snprintf(out_dir, max_size, "%s" PATH_SEP_STR "saves", buffer);
    } else {
        snprintf(out_dir, max_size, "." PATH_SEP_STR "saves");
    }
#endif
}

static void dump_sram(VM *vm) {
    // 1. Defensively check if we have a valid game ID initialized
    if (strlen(vm->id) == 0 || strncmp(vm->id, "BLANK", 5) == 0) {
        return; 
    }
    
    char saves_dir[512];
    get_saves_directory(saves_dir, sizeof(saves_dir));

    // 2. Ensure the "saves" directory exists
    mkdir(saves_dir);

    // 3. Construct the clean path format: saves/[id].dat
    char save_path[512];
    snprintf(save_path, sizeof(save_path), "%s" PATH_SEP_STR "%.8s.dat", saves_dir, vm->id);

    // 4. Stream the raw 8KB block directly out of VM memory to the host storage
    FILE *f = fopen(save_path, "wb");
    if (!f) {
        fprintf(stderr, "[SAVE ENGINE] Warning: Could not create save file at %s\n", save_path);
        return;
    } else {
        fwrite(&memory[ADDR_SRAM], sizeof(uint8_t), SRAM_SIZE, f);
        fclose(f);
    }
}

static void load_sram(VM *vm) {
    if (strlen(vm->id) == 0 || strncmp(vm->id, "BLANK", 5) == 0) {
        return;
    }

    char saves_dir[512];
    get_saves_directory(saves_dir, sizeof(saves_dir));
    
    char save_path[512];
    snprintf(save_path, sizeof(save_path), "%s" PATH_SEP_STR "%.8s.dat", saves_dir, vm->id);

    FILE *f = fopen(save_path, "rb");
    if (!f) {
        // If no save file exists yet, clear the SRAM bank to 0 so it's clean for the dev
        memset(&memory[ADDR_SRAM], 0, SRAM_SIZE);
        printf("i dont see a file gng\n");
        return;
    } else {
        fread(&memory[ADDR_SRAM], sizeof(uint8_t), SRAM_SIZE, f);
        fclose(f);
    }
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

static void title_handler(const char *path, long offset) {
    if (is_yfc) {
        // --- CARTRIDGE BINARY EXTRACTOR ---
        int fd = open(path, O_RDONLY | O_BINARY);
        if (fd >= 0) {
            lseek(fd, offset, SEEK_SET);
            char magic[4];
            char id[8];
            if (read(fd, magic, 4) == 4 && strncmp(magic, "YFC!", 4) == 0) {
                if (read(fd, id, 8) == 8) {
                    strncpy(game_id, id, 8);
                    // The title is stored immediately after the magic sig and id for 32 bytes
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
                
                // we're also gonna handle the game's id into it.
                char extracted_id[8] = {0};
                parse_config(buf, "id", extracted_id, sizeof(extracted_id));
                if (strlen(extracted_id) > 0) {
                    strncpy(game_id, extracted_id, 8);
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

    // Start scanning from the end of the file minus the sentinel size
    long current_pos = file_size - 8;
    char buf[8];

    // Scan backward byte-by-byte
    while (current_pos >= 0) {
        lseek(fd, current_pos, SEEK_SET);
        if (read(fd, buf, 8) == 8) {
            if (memcmp(buf, sentinel, 8) == 0) {
                /* Found it! Return the byte immediately AFTER the sentinel */
                return current_pos + 8; 
            }
        }
        current_pos--; // Move one byte backward
    }

    return -1;   /* No sentinel found */
}

static long find_appended(const char *exe_path) {
    int fd = open(exe_path, O_RDONLY | O_BINARY);
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

#ifdef __APPLE__
#include <dirent.h>

static const char *find_resources_cart(const char *exe_path) {
    static char cart_path[1024];
    char dir[1024];
    strncpy(dir, exe_path, sizeof(dir));

    /* strip /yf → Contents/MacOS */
    char *sl = strrchr(dir, '/');
    if (!sl) return NULL;
    *sl = '\0';

    /* strip /MacOS → Contents */
    sl = strrchr(dir, '/');
    if (!sl) return NULL;
    *sl = '\0';

    /* build Resources path */
    char res[1024];
    snprintf(res, sizeof(res), "%s/Resources", dir);

    DIR *d = opendir(res);
    if (!d) return NULL;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len > 4 && strcmp(ent->d_name + len - 4, ".yfc") == 0) {
            snprintf(cart_path, sizeof(cart_path),
                     "%s/%s", res, ent->d_name);
            closedir(d);
            return cart_path;
        }
    }
    closedir(d);
    return NULL;
}
#endif

int main(int argc, char *argv[]) {
    
    // initiate the system before argument handling
    mem_init();
    spu_init();
    vm_init(&vm);
    
    // first are we fused?
    long fused_offset = find_appended(argv[0]);
    
    /* mac resources bundle check */
    #ifdef __APPLE__
    {
        const char *res_cart = find_resources_cart(argv[0]);
        if (res_cart) {
            is_yfc = true;
            title_handler(res_cart, 0);
            strncpy(vm.id, game_id, 8);
            load_sram(&vm);
            yfc_boot(&vm, res_cart, 0);
            goto launch_window;
        }
    }
    /*
    #else
      if (fused_offset >= 0) {
          printf("[ENGINE] Fused game stream payload identified at byte offset: %ld\n", fused_offset);
          is_yfc = true;
          
          // We pass the engine's own running path as the cartridge target argument!
          title_handler(argv[0], fused_offset);
          strncpy(vm.id, game_id, 8);
          load_sram(&vm);
          yfc_boot(&vm, argv[0], fused_offset);
          goto launch_window;
      }
    */
    #endif

    if (argc < 2) {
        vm_bios(&vm);
        goto launch_window;
    }
    
    if (strcmp(argv[1], "--help") == 0) {
        printf("Usage:\n"
        "To run a script:    ./yf <script_name>\n"
        "To run a folder:    ./yf <cassette_folder>\n"
        "To pack a cart:     ./yf --package <cassette_folder> <cassette_name>\n"
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
    
    // --- RUNNING ANYTHING ---
    if (has_extension(target, ".yfc")) {
        is_yfc = true;
        title_handler(target, 0);
        strncpy(vm.id, game_id, 8);
        load_sram(&vm);
        yfc_boot(&vm, target, 0);
     } else if (has_extension(target, ".lua")) {
        is_yfc = false;
        vm_load(&vm, target);
     } else {
        is_yfc = false;
        if (chdir(target) != 0) {
            printf("ERROR: Could not open or find cartridge folder: %s\n", target);
            return 1;
        }
        title_handler(target, 0);
        strncpy(vm.id, game_id, 8);
        load_sram(&vm);
     }
    // i really like goto's now :)
    launch_window:
    
    kit_Context *ctx = kit_create(game_title, FB_WID, FB_HEI, KIT_SCALE4X);
    double dt;
    while (kit_step(ctx, &dt)) {
    
        if (!is_yfc) { vm_reload(&vm, "boot.lua"); }
        map_inputs(ctx);
        fb_expand(framebuf); 
        vm_update(&vm);
    
    }
    dump_sram(&vm);
    vm_shutdown(&vm);
    kit_destroy(ctx);
    return 0;
}

/* to check for appended carts at the end of the executable (linux/windows)
 though clang with a linux target handles this well and mac depends on the Resources folder for "fused" carts, 
 clang with a windows target does not put this at the end of the executable, causing a false positive. 
 thus the only way we can fix this is by using the MinGW GCC compiler or rewrite the fused checker entirely 
 in which i do not feel like doing rn so yeah */
const char end_of_executable[8] = {
    0xDE, 0xAD, 0xBE, 0xEF,
    0xCA, 0xFE, 0xBA, 0xBE
};

