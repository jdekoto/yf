// main.c
#define KIT_IMPL
#define SOKOL_IMPL
#define MG_IMPLEMENTATION
#ifdef _WIN32
  #define SOKOL_D3D11
  #include <d3d11.h>
#else
  #define SOKOL_GLCORE
#endif

#include "headers/kit.h"
#include "headers/mem.h"
#include "headers/vm.h"
#include "headers/audio.h"
#include "headers/font.h"
#include "headers/yfc.h"
#include "headers/config.h"
#include "headers/sokol/sokol_app.h"
#include "headers/sokol/sokol_gfx.h"
#include "headers/sokol/sokol_glue.h"
#include "headers/sokol/sokol_framebuffer.h"
#include "headers/sokol/sokol_letterbox.h"
#include "headers/minigamepad.h"
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
uint16_t framebuf[FB_WID * FB_HEI];
static bool is_yfc = false;
static bool empty_rom = false;
static bool single = false;
static bool is_fused = false;
static char game_path[512];
static char game_title[32];
static char game_id[8];
static mg_gamepads pads;
static long fused_offset = 0;

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

// Define a safe max index. 512 leaves plenty of buffer room.
#define KEY_MAX 512

static bool key_state[KEY_MAX];

void event(const sapp_event *e) {
    if (e->type == SAPP_EVENTTYPE_KEY_DOWN) {
        // Ensure index is within our array bounds
        if (e->key_code < KEY_MAX) {
            key_state[e->key_code] = true;
        }
    } 
    else if (e->type == SAPP_EVENTTYPE_KEY_UP) {
        if (e->key_code < KEY_MAX) {
            key_state[e->key_code] = false;
        }
    }
}

static inline bool kdown(int code) { 
    return (code >= 0 && code < KEY_MAX) ? key_state[code] : false; 
}


void map_inputs(void) {
    
    mg_gamepad *pad1 = pads.list.head;
    mg_gamepad *pad2 = (pad1) ? pad1->next : NULL;
    
    // Refresh controller connection states
    
    poke(0x06444, peek(0x06440));
    poke(0x06445, peek(0x06441));
    poke(0x06446, peek(0x06442));
    poke(0x06447, peek(0x06443));

    uint32_t final_mask = 0;

    // --- PLAYER 1 SUB-MASK MAPPING (Bits 0-8) ---
    uint16_t p1_mask = 0;
    if (kdown(SAPP_KEYCODE_LEFT))    p1_mask |= (1 << 0);
    if (kdown(SAPP_KEYCODE_RIGHT))   p1_mask |= (1 << 1);
    if (kdown(SAPP_KEYCODE_UP))      p1_mask |= (1 << 2);
    if (kdown(SAPP_KEYCODE_DOWN))    p1_mask |= (1 << 3);
    if (kdown(SAPP_KEYCODE_A))       p1_mask |= (1 << 4); 
    if (kdown(SAPP_KEYCODE_S))       p1_mask |= (1 << 5); 
    if (kdown(SAPP_KEYCODE_Z))       p1_mask |= (1 << 6); 
    if (kdown(SAPP_KEYCODE_X))       p1_mask |= (1 << 7); 
    if (kdown(SAPP_KEYCODE_ENTER))   p1_mask |= (1 << 8);

    if (pad1 && pad1->connected) {
        if (mg_gamepad_button_is_pressed(pad1, MG_BUTTON_DPAD_LEFT))  p1_mask |= (1 << 0);
        if (mg_gamepad_button_is_pressed(pad1, MG_BUTTON_DPAD_RIGHT)) p1_mask |= (1 << 1);
        if (mg_gamepad_button_is_pressed(pad1, MG_BUTTON_DPAD_UP))    p1_mask |= (1 << 2);
        if (mg_gamepad_button_is_pressed(pad1, MG_BUTTON_DPAD_DOWN))  p1_mask |= (1 << 3);
        if (mg_gamepad_button_is_pressed(pad1, MG_BUTTON_SOUTH))      p1_mask |= (1 << 4); // A
        if (mg_gamepad_button_is_pressed(pad1, MG_BUTTON_EAST))       p1_mask |= (1 << 5); // B
        if (mg_gamepad_button_is_pressed(pad1, MG_BUTTON_WEST))       p1_mask |= (1 << 6); // X
        if (mg_gamepad_button_is_pressed(pad1, MG_BUTTON_NORTH))      p1_mask |= (1 << 7); // Y
        if (mg_gamepad_button_is_pressed(pad1, MG_BUTTON_START))      p1_mask |= (1 << 8);

        // Analog D-Pad Deadzone Fallback
        float ax = mg_gamepad_axis_value(pad1, MG_AXIS_LEFT_X);
        float ay = mg_gamepad_axis_value(pad1, MG_AXIS_LEFT_Y);
        if (ax < -0.5f) p1_mask |= (1 << 0);
        if (ax >  0.5f) p1_mask |= (1 << 1);
        if (ay < -0.5f) p1_mask |= (1 << 2);
        if (ay >  0.5f) p1_mask |= (1 << 3);
    }
    final_mask |= p1_mask;

    // --- PLAYER 2 MAPPING (Gamepad 2) ---
    if (pad2 && pad2->connected) {
        uint16_t p2_mask = 0;
        if (mg_gamepad_button_is_pressed(pad2, MG_BUTTON_DPAD_LEFT))  p2_mask |= (1 << 0);
        if (mg_gamepad_button_is_pressed(pad2, MG_BUTTON_DPAD_RIGHT)) p2_mask |= (1 << 1);
        if (mg_gamepad_button_is_pressed(pad2, MG_BUTTON_DPAD_UP))    p2_mask |= (1 << 2);
        if (mg_gamepad_button_is_pressed(pad2, MG_BUTTON_DPAD_DOWN))  p2_mask |= (1 << 3);
        if (mg_gamepad_button_is_pressed(pad2, MG_BUTTON_SOUTH))      p2_mask |= (1 << 4);
        if (mg_gamepad_button_is_pressed(pad2, MG_BUTTON_EAST))       p2_mask |= (1 << 5);
        if (mg_gamepad_button_is_pressed(pad2, MG_BUTTON_WEST))       p2_mask |= (1 << 6);
        if (mg_gamepad_button_is_pressed(pad2, MG_BUTTON_NORTH))      p2_mask |= (1 << 7);
        if (mg_gamepad_button_is_pressed(pad2, MG_BUTTON_START))      p2_mask |= (1 << 8);

        float ax = mg_gamepad_axis_value(pad2, MG_AXIS_LEFT_X);
        float ay = mg_gamepad_axis_value(pad2, MG_AXIS_LEFT_Y);
        if (ax < -0.5f) p2_mask |= (1 << 0);
        if (ax >  0.5f) p2_mask |= (1 << 1);
        if (ay < -0.5f) p2_mask |= (1 << 2);
        if (ay >  0.5f) p2_mask |= (1 << 3);

        final_mask |= ((uint32_t)p2_mask << 9);
    }
    
    // 3. Poke the 32-bit aggregated mask cleanly across the 4 sequential bytes
    poke(0x06440, (uint8_t)(final_mask & 0xFF));
    poke(0x06441, (uint8_t)((final_mask >> 8) & 0xFF));
    poke(0x06442, (uint8_t)((final_mask >> 16) & 0xFF));
    poke(0x06443, (uint8_t)((final_mask >> 24) & 0xFF));
    
    mg_gamepads_poll(&pads); 
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
            if (read(fd, magic, 4) == 4 && 
                  magic[0] == 'Y' && magic[1] == 'F' && magic[2] == 'C' && magic[3] == '!') {
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
// is the fused an actual rom of just compiled code?
static bool validate_yfc(FILE *f, long offset, long file_size) {
    // must be a 84 byte header
    if (file_size - offset < 84) return false;

    long orig_pos = ftell(f);
    fseek(f, offset, SEEK_SET);

    char hdr[84];
    size_t read_bytes = fread(hdr, 1, 84, f);
    fseek(f, orig_pos, SEEK_SET); // restore file cursor

    if (read_bytes != 84) return false;

    if (hdr[0] != 'Y' || hdr[1] != 'F' || hdr[2] != 'C' || hdr[3] != '!') { // magic 4-byte
        return false;
    }

    for (int i = 4; i < 12; i++) { // rom id
        unsigned char c = (unsigned char)hdr[i];
        if (c != 0 && (c < 32 || c > 126)) return false;
    }

    for (int i = 12; i < 44; i++) { // game title
        unsigned char c = (unsigned char)hdr[i];
        if (c != 0 && (c < 32 || c > 126)) return false;
    }

    for (int i = 44; i < 76; i++) { // game author
        unsigned char c = (unsigned char)hdr[i];
        if (c != 0 && (c < 32 || c > 126)) return false;
    }
                                                                 
    for (int i = 76; i < 84; i++) { // game version
        unsigned char c = (unsigned char)hdr[i];
        if (c != 0 && (c < 32 || c > 126)) return false;
    }
                                                          
    return true;
}

static long find_appended(const char *exe_path) {
    FILE *f = fopen(exe_path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size < 1024) {
        fclose(f);
        return -1;
    }

    #define BUF_SZ 4096
    char buf[BUF_SZ];
    long search_pos = file_size;

    while (search_pos > 0) {
        long chunk_size = (search_pos < BUF_SZ) ? search_pos : BUF_SZ;
        search_pos -= chunk_size;

        fseek(f, search_pos, SEEK_SET);
        if (fread(buf, 1, chunk_size, f) != (size_t)chunk_size) break;

        // Scan backward through chunk
        for (long i = chunk_size - 4; i >= 0; i--) {
            if (buf[i] == 'Y' && buf[i+1] == 'F' && buf[i+2] == 'C' && buf[i+3] == '!') {
                long candidate_offset = search_pos + i;

                // Validate if this is a real YFC cartridge header or compiler junk
                if (validate_yfc(f, candidate_offset, file_size)) {
                    fclose(f);
                    return candidate_offset;
                }
            }
        }

        if (search_pos > 0) search_pos += 3; // Keep boundary alignment
    }

    fclose(f);
    return -1;
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


static sg_pass_action pass_action;
static sfb_framebuffer fb;
static uint32_t fb_rgba[FB_WID * FB_HEI]; 

void init(void) {
    
    // initiate the system before argument handling
    mem_init();
    spu_init();
    mg_gamepads_init(&pads);
    vm_init(&vm);
    
    if (empty_rom) { 
        vm_bios(&vm); 
    } else if (is_fused) {
        yfc_boot(&vm, game_path, fused_offset);
    } else if (is_yfc) { 
        yfc_boot(&vm, game_path, 0); 
    } else if (single) {
        vm_load(&vm, game_path);  // Explicitly load single script on startup
    } else {
        vm_load(&vm, "boot.lua"); // Explicitly load folder-based game on startup
    }
    
    load_sram(&vm);
    
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
    sfb_setup(&(sfb_desc){ .logger.func = slog_func });
    
    fb = sfb_make_framebuffer(&(sfb_framebuffer_desc){
        .width  = FB_WID,
        .height = FB_HEI,
        .format = SFB_FORMAT_RGBA8,     /* default, but explicit is clearer */
        .prescale = 4,
    });

    pass_action.colors[0] = (sg_color_attachment_action){
    .load_action = SG_LOADACTION_CLEAR,
    .clear_value = { 0, 0, 0, 1 },
};
}

static void expand_rgb565_to_rgba8(const uint16_t *src, uint32_t *dst, int count) {
    for (int i = 0; i < count; i++) {
        uint16_t px = src[i];
        uint8_t r5 = (px >> 11) & 0x1F;
        uint8_t g6 = (px >> 5)  & 0x3F;
        uint8_t b5 =  px        & 0x1F;

        /* bit-replicate up to 8 bits, standard RGB565->RGB888 expansion */
        uint8_t r8 = (r5 << 3) | (r5 >> 2);
        uint8_t g8 = (g6 << 2) | (g6 >> 4);
        uint8_t b8 = (b5 << 3) | (b5 >> 2);

        dst[i] = (uint32_t)0xFF << 24 | (uint32_t)b8 << 16 | (uint32_t)g8 << 8 | r8;
    }
}

void frame(void) {
    static int reload_timer = 0;
    reload_timer++;
    
    if (reload_timer >= 30) {
        reload_timer = 0; // Reset timer
        
        // Only run hot-reloading if we are running local uncompiled files
        if (!is_yfc) { 
            if (single) { 
                vm_reload(&vm, game_path); 
            } else { 
                vm_reload(&vm, "boot.lua"); 
            }
        }
    }             
    vm_update(&vm);
    fb_expand(framebuf);
    map_inputs();
    /* fb to rgba8 conversion */
    expand_rgb565_to_rgba8(framebuf, fb_rgba, FB_WID * FB_HEI);

    sfb_update(fb, &(sfb_update_desc){
        .pixels = SG_RANGE(fb_rgba),
    });
    
    slbx_viewport vp = slbx_letterbox(sapp_width(), sapp_height(), &(slbx_letterbox_desc){
        .content_aspect_ratio = (float)FB_WID / (float)FB_HEI,
    });
    
    sg_begin_pass(&(sg_pass){ .action = pass_action, .swapchain = sglue_swapchain() });
    sg_apply_viewport(vp.x, vp.y, vp.width, vp.height, true);
    sfb_render_ex(fb, &(sfb_render_desc){ .use_nearest_filter = true });
    sg_end_pass();
    sg_commit();
    
}

void cleanup(void) {
    dump_sram(&vm);
    vm_shutdown(&vm);
    // causes the game to freeze and exit???
    // mg_gamepads_free(&pads); 
    spu_shutdown();
}

static void on_launch(int argc, char *argv[]) {
    // first are we fused?
    #ifdef __APPLE__
    {
        const char *res_cart = find_resources_cart(argv[0]);
        if (res_cart) {
            is_yfc = true;
            title_handler(res_cart, 0);
            strncpy(vm.id, game_id, 8);
            strncpy(game_path, res_cart, 512);
            return;
        }
    }
    #else
        long offset = find_appended(argv[0]);
        if (offset >= 0) {
            printf("[ENGINE] Fused game stream payload identified at byte offset: %ld\n", offset);
            is_yfc = true;
            is_fused = true;
            fused_offset = offset;
            title_handler(argv[0], fused_offset);
            strncpy(vm.id, game_id, 8);
            strncpy(game_path, argv[0], 512);
            return;
        }
    
    #endif
    
    if (argc < 2) {
        empty_rom = true;
        return;
    }
    
    if (strcmp(argv[1], "--help") == 0) {
        printf("Usage:\n"
        "To run a script:    ./yf <script_name>\n"
        "To run a folder:    ./yf <cassette_folder>\n"
        "To pack a cart:     ./yf --package <cassette_folder> <cassette_name>\n"
        );
        exit(1);
    }

    // --- HANDLE PACKAGING ARGUMENT ---
    
    if (strcmp(argv[1], "--package") == 0) {
        if (argc < 3) {
            printf("Error: Please specify a folder to package.\n");
            exit(1);
        }
        printf("Packaging %s into a standalone .yfc cartridge...\n", argv[2]);
        yfc_pack(argv[2], argv[3]);
        exit(0);
    }
    
    const char *target = argv[1];
    
    // --- RUNNING ANYTHING ---
    if (has_extension(target, ".yfc")) {
        is_yfc = true;
        title_handler(target, 0);
        strncpy(game_path, target, 512);
     } else if (has_extension(target, ".lua")) {
        single = true;
        strncpy(game_path, target, 512);
     } else {
        if (chdir(target) != 0) {
            printf("ERROR: Could not open or find cartridge folder: %s\n", target);
            exit(1);
        }
        title_handler(target, 0);
        strncpy(vm.id, game_id, 8);
     }
}

sapp_desc sokol_main(int argc, char *argv[]) {
    on_launch(argc, argv);
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .event_cb = event,
        .width = FB_WID * 4,
        .height = FB_HEI * 4,
        .window_title = game_title,
    };
}

