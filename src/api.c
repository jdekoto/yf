/*
 * api.c — Yellow Feather Core Engine C API
 * Complete 1:1 implementation from the original LUA API
 */

#include "api.h"
#include "mem.h"
#include "audio.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <lua.h>
#include <lauxlib.h>

/* ── font constants (must match mem_init packing) ────────────── */
#define FONT_FIRST      32
#define FONT_CHAR_W      5
#define FONT_CHAR_H      5
#define FONT_STRIDE      6   /* bytes per char in RAM (5 rows + 1 pad) */
#define FONT_CHAR_ADV    6   /* pixel advance per character            */

/* ── sprite constants ────────────────────────────────────────── */
#define SPR_W            8
#define SPR_H            8
#define SPR_BANK_DIM    64   /* sprite bank is 64×64 pixels           */
#define SPR_PER_ROW     (SPR_BANK_DIM / SPR_W)   /* 8 sprites per row */

/* ── tilemap constants ───────────────────────────────────────── */
#define MAP_WIDTH      256
#define MAP_HEIGHT     192

/* ── active sprite bank (toggled by sbank()) ─────────────────── */
static uint32_t g_sprbank = ADDR_SPRB0;

/* ── global drawing configuration states ────────────────────── */
static int g_cam_x = 0;
static int g_cam_y = 0;

static int g_clip_enabled = 0;
static int g_clip_x0 = 0;
static int g_clip_y0 = 0;
static int g_clip_x1 = FB_WID - 1;
static int g_clip_y1 = FB_HEI - 1;

/* ── native engine 16-color palette storage ──────────────────── */
/* ── 16-bit color packing formula ────────────────────────────── */
// Standard RGB565 packing (5 bits Red, 6 bits Green, 5 bits Blue)
#define RGB_CONVERT(r, g, b) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
static uint16_t g_palette[16];

/* ── save data handling ──────────────────────────────────────── */
#define CARTDATA_SLOTS 64

static char g_cartdata_path[512] = {0};
static int32_t g_cartdata_ram[CARTDATA_SLOTS] = {0};
static bool g_cartdata_active = false;

/* ═══════════════════════════════════════════════════════════════
   INTERNAL PIXEL & COLOR HELPERS
   ═══════════════════════════════════════════════════════════════ */

/* Compresses 24-bit RGB values into single native RGB565 words */
static inline uint16_t _make_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t r5 = (r * 31) / 255;
    uint16_t g6 = (g * 63) / 255;
    uint16_t b5 = (b * 31) / 255;
    return (r5 << 11) | (g6 << 5) | b5;
}

/* Resolves color input: if color is 0-15, fetch palette indexed color; otherwise treat as raw RGB565 */
static inline uint16_t _resolve_color(int col) {
    if (col >= 0 && col < 16) {
        return g_palette[col];
    }
    return (uint16_t)col;
}

/* Natively handles camera transforms, clip windows, and memory-poking */
static inline void _pset(int x, int y, int col) {
    // 1. Shift by global camera offset
    x -= g_cam_x;
    y -= g_cam_y;

    // 2. Hardware Clipping Window Bounds check
    if (g_clip_enabled) {
        if (x < g_clip_x0 || x > g_clip_x1 || y < g_clip_y0 || y > g_clip_y1) return;
    }

    // 3. Strict physical frame-buffer clipping
    if (x < 0 || x >= FB_WID || y < 0 || y >= FB_HEI) return;

    // 4. Translate and write word cleanly using standard low-level hardware abstraction
    uint16_t color16 = _resolve_color(col);
    uint32_t addr = ADDR_FB + (uint32_t)(y * FB_WID + x) * 2;
    poke2(addr, color16);
}

static inline uint16_t _pget(int x, int y) {
    if (x < 0 || x >= FB_WID || y < 0 || y >= FB_HEI) return 0;
    return peek2(ADDR_FB + (uint32_t)(y * FB_WID + x) * 2);
}

/* ═══════════════════════════════════════════════════════════════
   GRAPHICS API
   ═══════════════════════════════════════════════════════════════ */

/* cls([col]) — clear screen to color index or raw RGB565 color */
static int l_cls(lua_State *L) {
    int col_param = (int)luaL_optinteger(L, 1, 0);
    uint16_t color16 = _resolve_color(col_param);
    
    // Clear whole frame buffer sequentially via standard poke2 iterations
    for (uint32_t addr = ADDR_FB; addr < ADDR_FB + (FB_WID * FB_HEI * 2); addr += 2) {
        poke2(addr, color16);
    }
    return 0;
}

/* pset(x, y, col) */
static int l_pset(lua_State *L) {
    _pset((int)luaL_checknumber(L, 1),
          (int)luaL_checknumber(L, 2),
          (int)luaL_checknumber(L, 3));
    return 0;
}

/* pget(x, y) → col */
static int l_pget(lua_State *L) {
    lua_pushinteger(L, _pget((int)luaL_checknumber(L, 1),
                             (int)luaL_checknumber(L, 2)));
    return 1;
}

/* camera([x, y]) — apply global hardware camera view displacement */
static int l_camera(lua_State *L) {
    g_cam_x = (int)luaL_optinteger(L, 1, 0);
    g_cam_y = (int)luaL_optinteger(L, 2, 0);
    return 0;
}

/* clip([x, y, w, h]) — slice render window space limits */
static int l_clip(lua_State *L) {
    if (lua_isnoneornil(L, 1) || lua_isnoneornil(L, 2) || lua_isnoneornil(L, 3) || lua_isnoneornil(L, 4)) {
        g_clip_enabled = 0;
        g_clip_x0 = 0;
        g_clip_y0 = 0;
        g_clip_x1 = FB_WID - 1;
        g_clip_y1 = FB_HEI - 1;
    } else {
        g_clip_enabled = 1;
        int x = (int)luaL_checknumber(L, 1);
        int y = (int)luaL_checknumber(L, 2);
        int w = (int)luaL_checknumber(L, 3);
        int h = (int)luaL_checknumber(L, 4);
        g_clip_x0 = x;
        g_clip_y0 = y;
        g_clip_x1 = x + w - 1;
        g_clip_y1 = y + h - 1;
    }
    return 0;
}

/* line(x0, y0, x1, y1, col) — Bresenham Line Render */
static int l_line(lua_State *L) {
    int x0  = (int)luaL_checknumber(L, 1);
    int y0  = (int)luaL_checknumber(L, 2);
    int x1  = (int)luaL_checknumber(L, 3);
    int y1  = (int)luaL_checknumber(L, 4);
    int col = (int)luaL_checknumber(L, 5);

    int dx  =  abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy  = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        _pset(x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return 0;
}

/* rect(x0, y0, x1, y1, col) — hollow wireframe rectangle bound */
static int l_rect(lua_State *L) {
    int x0  = (int)luaL_checknumber(L, 1);
    int y0  = (int)luaL_checknumber(L, 2);
    int x1  = (int)luaL_checknumber(L, 3);
    int y1  = (int)luaL_checknumber(L, 4);
    int col = (int)luaL_checknumber(L, 5);

    int start_x = x0 < x1 ? x0 : x1;
    int end_x   = x0 > x1 ? x0 : x1;
    int start_y = y0 < y1 ? y0 : y1;
    int end_y   = y0 > y1 ? y0 : y1;

    for (int x = start_x; x <= end_x; x++) {
        _pset(x, start_y, col);
        _pset(x, end_y,   col);
    }
    for (int y = start_y; y <= end_y; y++) {
        _pset(start_x, y, col);
        _pset(end_x,   y, col);
    }
    return 0;
}

/* rectfill(x0, y0, x1, y1, col) — solid filled rectangular area */
static int l_rectfill(lua_State *L) {
    int x0  = (int)luaL_checknumber(L, 1);
    int y0  = (int)luaL_checknumber(L, 2);
    int x1  = (int)luaL_checknumber(L, 3);
    int y1  = (int)luaL_checknumber(L, 4);
    int col = (int)luaL_checknumber(L, 5);

    int start_x = x0 < x1 ? x0 : x1;
    int end_x   = x0 > x1 ? x0 : x1;
    int start_y = y0 < y1 ? y0 : y1;
    int end_y   = y0 > y1 ? y0 : y1;

    for (int y = start_y; y <= end_y; y++) {
        for (int x = start_x; x <= end_x; x++) {
            _pset(x, y, col);
        }
    }
    return 0;
}

/* circ(cx, cy, r, col) — hollow circle midpoint algorithm */
static int l_circ(lua_State *L) {
    int cx  = (int)luaL_checknumber(L, 1);
    int cy  = (int)luaL_checknumber(L, 2);
    int r   = (int)luaL_checknumber(L, 3);
    int col = (int)luaL_checknumber(L, 4);

    if (r < 0) return 0;
    if (r == 0) {
        _pset(cx, cy, col);
        return 0;
    }

    int x = 0;
    int y = r;
    int d = 3 - (2 * r);

    while (x <= y) {
        _pset(cx + x, cy + y, col); _pset(cx - x, cy + y, col);
        _pset(cx + x, cy - y, col); _pset(cx - x, cy - y, col);
        _pset(cx + y, cy + x, col); _pset(cx - y, cy + x, col);
        _pset(cx + y, cy - x, col); _pset(cx - y, cy - x, col);
        
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + (4 * x) + 6;
        }
    }
    return 0;
}

/* circfill(cx, cy, r, col) — clean horizontal line segment filled circle */
static int l_circfill(lua_State *L) {
    int cx  = (int)luaL_checknumber(L, 1);
    int cy  = (int)luaL_checknumber(L, 2);
    int r   = (int)luaL_checknumber(L, 3);
    int col = (int)luaL_checknumber(L, 4);

    if (r < 0) return 0;

    int x = r;
    int y = 0;
    int err = 1 - r;

    while (x >= y) {
        for (int i = cx - x; i <= cx + x; i++) {
            _pset(i, cy + y, col);
            _pset(i, cy - y, col);
        }
        for (int i = cx - y; i <= cx + y; i++) {
            _pset(i, cy + x, col);
            _pset(i, cy - x, col);
        }
        
        y++;
        if (err < 0) {
            err = err + 2 * y + 1;
        } else {
            x--;
            err = err + 2 * (y - x) + 1;
        }
    }
    return 0;
}

/* sbank(n) — switch active sprite bank (0 = SPRB0, 1 = SPRB1) */
static int l_sbank(lua_State *L) {
    int n = (int)luaL_checknumber(L, 1);
    g_sprbank = (n == 1) ? ADDR_SPRB1 : ADDR_SPRB0;
    return 0;
}

// Track spreadsheet dimensions directly inside the native runtime layer
static uint32_t bank_addresses[] = { 0x06500, 0x08500 };
static int bank_widths[]         = { 64, 64 };
static int bank_heights[]        = { 64, 64 };

/* spr(id, x, y, [w], [h], [flip_x], [flip_y])
renders sprites (yay!) width and height are in tiles,
not pixels. */
int l_spr(lua_State *L) {
    int id       = (int)luaL_checknumber(L, 1);
    int screen_x = (int)luaL_checknumber(L, 2);
    int screen_y = (int)luaL_checknumber(L, 3);
    int w        = (int)luaL_optnumber(L, 4, 1);
    int h        = (int)luaL_optnumber(L, 5, 1);
    bool flip_x  = lua_toboolean(L, 6);
    bool flip_y  = lua_toboolean(L, 7);

    uint8_t current_bank = memory[0x06044u]; // ADDR_BANK_SWITCH
    if (current_bank > 1) current_bank = 0;

    uint32_t sheet_base = bank_addresses[current_bank];
    int sheet_width     = bank_widths[current_bank];

    int total_cols = sheet_width / 8;
    if (total_cols <= 0) return 0;

    int spr_x = (id % total_cols) * 8;
    int spr_y = (id / total_cols) * 8;

    int total_w = w * 8;
    int total_h = h * 8;

    for (int py = 0; py < total_h; py++) {
        for (int px = 0; px < total_w; px++) {
            int dest_x = screen_x + px;
            int dest_y = screen_y + py;

            // Direct hardware hardware clipping check (128x96 screen bounds)
            if (dest_x < 0 || dest_x >= 128 || dest_y < 0 || dest_y >= 96) {
                continue;
            }

            // Composite layout reflection mirror matrix
            int target_src_x = flip_x ? (total_w - 1 - px) : px;
            int target_src_y = flip_y ? (total_h - 1 - py) : py;

            int source_x = spr_x + target_src_x;
            int source_y = spr_y + target_src_y;

            if (source_x < 0 || source_x >= sheet_width) continue;

            // Extract 16-bit color elements
            uint32_t src_addr = sheet_base + ((source_y * sheet_width + source_x) * 2);
            uint8_t low = memory[src_addr];
            uint8_t high = memory[src_addr + 1];
            uint16_t color16 = low | (high << 8);

            // Transparent color key
            if (color16 != 0x0000) {
                uint32_t fb_addr = ADDR_FB + ((dest_y * 128 + dest_x) * 2);
                memory[fb_addr]     = low;
                memory[fb_addr + 1] = high;
            }
        }
    }
    return 0;
}

/* sprsht(filename, bank) - load a 64*64 into one of two sprite banks*/
int l_sprsht(lua_State *L) {
    const char* filename = luaL_checkstring(L, 1);
    int bank_id = (int)luaL_optinteger(L, 2, 0);

    if (bank_id < 0 || bank_id > 1) {
        lua_pushboolean(L, false);
        return 1;
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        lua_pushboolean(L, false);
        return 1;
    }

    uint8_t header[54];
    if (fread(header, 1, 54, file) != 54 || header[0] != 'B' || header[1] != 'M') {
        fclose(file);
        lua_pushboolean(L, false);
        return 1;
    }

    uint32_t data_offset = *(uint32_t*)&header[10];
    int32_t width        = *(int32_t*)&header[18];
    int32_t height       = *(int32_t*)&header[22];
    uint16_t bpp         = *(uint16_t*)&header[28];

    if (bpp != 24) {
        fclose(file);
        lua_pushboolean(L, false);
        return 1;
    }

    int abs_height = (height < 0) ? -height : height;
    if (width > 64 || abs_height > 64) {
        fclose(file);
        lua_pushboolean(L, false);
        return 1;
    }

    // Cache metrics natively so l_spr can grab them immediately
    bank_widths[bank_id]  = width;
    bank_heights[bank_id] = abs_height;

    fseek(file, data_offset, SEEK_SET);
    int row_size = ((24 * width + 31) / 32) * 4;
    uint8_t *pixel_bytes = malloc(row_size * abs_height);
    fread(pixel_bytes, 1, row_size * abs_height, file);
    fclose(file);

    uint32_t dest_address = bank_addresses[bank_id];
    bool is_bottom_up = (height > 0);

    for (int y = 0; y < abs_height; y++) {
        int bmp_y = is_bottom_up ? (abs_height - 1 - y) : y;
        int row_start = bmp_y * row_size;

        for (int x = 0; x < width; x++) {
            int pos = row_start + (x * 3);
            uint8_t b = pixel_bytes[pos];
            uint8_t g = pixel_bytes[pos + 1];
            uint8_t r = pixel_bytes[pos + 2];

            // Native hardware RGB565 packing math transformation
            uint16_t r5 = (r * 31) / 255;
            uint16_t g6 = (g * 63) / 255;
            uint16_t b5 = (b * 31) / 255;
            uint16_t color16 = (r5 << 11) | (g6 << 5) | b5;

            uint32_t target_addr = dest_address + ((y * width + x) * 2);
            memory[target_addr]     = color16 & 0xFF;
            memory[target_addr + 1] = (color16 >> 8) & 0xFF;
        }
    }

    free(pixel_bytes);
    lua_pushboolean(L, true);
    return 1;
}

/* ── custom font lookup map ─────────────────────────────────── */
static int g_ascii_to_font_index[256];
static int g_font_map_initialized = 0;

static void init_font_map(void) {
    if (g_font_map_initialized) return;

    for (int i = 0; i < 256; i++) {
        g_ascii_to_font_index[i] = -1;
    }

    const char *sequential_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ !__0123456789.:(){}-+/*,=\"'_[]____?<>@#$%^__~";
    int len = (int)strlen(sequential_chars);
    for (int i = 0; i < len; i++) {
        unsigned char b = (unsigned char)sequential_chars[i];
        g_ascii_to_font_index[b] = i; /* 0-indexed matches Lua's i - 1 */
    }

    /* Manual character position overrides matching your Lua setup */
    g_ascii_to_font_index[(unsigned char)'_'] = 43;  
    g_ascii_to_font_index[(unsigned char)'['] = 44;  
    g_ascii_to_font_index[(unsigned char)']'] = 45;  
    g_ascii_to_font_index[(unsigned char)'{'] = 47;  
    g_ascii_to_font_index[(unsigned char)'}'] = 48;  
    g_ascii_to_font_index[(unsigned char)'^'] = 49;  
    g_ascii_to_font_index[(unsigned char)'?'] = 50;  
    g_ascii_to_font_index[(unsigned char)'<'] = 51;  
    g_ascii_to_font_index[(unsigned char)'>'] = 52;  
    g_ascii_to_font_index[(unsigned char)'@'] = 53;  
    g_ascii_to_font_index[(unsigned char)'#'] = 54;  
    g_ascii_to_font_index[(unsigned char)'$'] = 55;  
    g_ascii_to_font_index[(unsigned char)'%'] = 56;  
    g_ascii_to_font_index[(unsigned char)'&'] = 57;  
    g_ascii_to_font_index[(unsigned char)'~'] = 58;  

    g_font_map_initialized = 1;
}

/* Internal proportional character width scanner */
static int _get_char_width(uint32_t font_char_addr) {
    int max_col = 0;
    for (int row = 0; row < FONT_CHAR_H; row++) {
        uint8_t row_byte = peek(font_char_addr + row);
        for (int col = 0; col < FONT_CHAR_W; col++) {
            uint8_t bit_mask = 0x80 >> col;
            if ((row_byte & bit_mask) != 0) {
                if (col > max_col) max_col = col;
            }
        }
    }
    if (max_col == 0) return 2;
    return max_col + 1;
}

/* text(str, x, y, [col]) 
   16-Bit Proportional Pro-Text Printer */
static int l_text(lua_State *L) {
    const char *str = luaL_checkstring(L, 1);
    int start_x     = (int)luaL_checknumber(L, 2);
    int y           = (int)luaL_checknumber(L, 3);
    /* Default to pure white color (0xFFFF) if no argument is provided */
    uint16_t color  = (uint16_t)luaL_optinteger(L, 4, 0xFFFF);

    int x = start_x;

    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        unsigned char c = *p;
        
        /* Inline upper casing: string.upper(str) */
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }

        if (c == 10) { /* Newline literal character (\n) */
            x = start_x;
            y += 6; 
        } else {
            int idx = g_ascii_to_font_index[c];
            if (idx != -1) {
                uint32_t font_char_addr = ADDR_FONT + (uint32_t)idx * FONT_STRIDE;
                int char_width = _get_char_width(font_char_addr);
                
                for (int row = 0; row < FONT_CHAR_H; row++) {
                    uint8_t row_byte = peek(font_char_addr + row);
                    for (int col = 0; col < char_width; col++) {
                        uint8_t bit_mask = 0x80 >> col;
                        if ((row_byte & bit_mask) != 0) {
                            _pset(x + col, y + row, color);
                        }
                    }
                }
                x += char_width + 1;
            } else {
                x += 4; /* Advance for undefined characters */
            }
        }
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   INPUT API
   ═══════════════════════════════════════════════════════════════ */

/* btn(n) → bool */
static int l_btn(lua_State *L) {
    int n = (int)luaL_checknumber(L, 1);
    uint16_t cur = (uint16_t)(peek(ADDR_INPUT) | (peek(ADDR_INPUT + 1) << 8));
    lua_pushboolean(L, (cur >> n) & 1);
    return 1;
}

/* btnp(n) → bool */
static int l_btnp(lua_State *L) {
    int n = (int)luaL_checknumber(L, 1);
    uint16_t cur  = (uint16_t)(peek(ADDR_INPUT)     | (peek(ADDR_INPUT + 1) << 8));
    uint16_t prev = (uint16_t)(peek(ADDR_INPUT + 2)  | (peek(ADDR_INPUT + 3) << 8));
    lua_pushboolean(L, ((cur >> n) & 1) && !((prev >> n) & 1));
    return 1;
}

/* ═══════════════════════════════════════════════════════════════
   TILEMAP API
   ═══════════════════════════════════════════════════════════════ */

/* tile(x, y, [id]) → id */
static int l_tile(lua_State *L) {
    int x = (int)luaL_checknumber(L, 1);
    int y = (int)luaL_checknumber(L, 2);

    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        if (lua_isnoneornil(L, 3)) { lua_pushinteger(L, 0); return 1; }
        return 0;
    }

    uint32_t addr = ADDR_MAP + (uint32_t)(y * MAP_WIDTH + x);
    if (lua_isnoneornil(L, 3)) {
        lua_pushinteger(L, peek(addr));
        return 1;
    }
    poke(addr, (uint8_t)luaL_checknumber(L, 3));
    return 0;
}

/* mget(x, y) → id */
static int l_mget(lua_State *L) {
    int x = (int)luaL_checknumber(L, 1);
    int y = (int)luaL_checknumber(L, 2);
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        lua_pushinteger(L, 0); return 1;
    }
    lua_pushinteger(L, peek(ADDR_MAP + (uint32_t)(y * MAP_WIDTH + x)));
    return 1;
}

/* mset(x, y, id) */
static int l_mset(lua_State *L) {
    int x  = (int)luaL_checknumber(L, 1);
    int y  = (int)luaL_checknumber(L, 2);
    int id = (int)luaL_checknumber(L, 3);
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) return 0;
    poke(ADDR_MAP + (uint32_t)(y * MAP_WIDTH + x), (uint8_t)id);
    return 0;
}

/* map(start_tx, start_ty, screen_x, screen_y, [tiles_w], [tiles_h]) */
static int l_map(lua_State *L) {
    int stx = (int)luaL_checknumber(L, 1);
    int sty = (int)luaL_checknumber(L, 2);
    int scx = (int)luaL_checknumber(L, 3);
    int scy = (int)luaL_checknumber(L, 4);
    int tw  = (int)luaL_optinteger(L, 5, FB_WID / SPR_W);
    int th  = (int)luaL_optinteger(L, 6, FB_HEI / SPR_H);

    for (int ty = 0; ty < th; ty++) {
        int my = sty + ty;
        if (my < 0 || my >= MAP_HEIGHT) continue;

        for (int tx = 0; tx < tw; tx++) {
            int mx = stx + tx;
            if (mx < 0 || mx >= MAP_WIDTH) continue;

            uint8_t id = peek(ADDR_MAP + (uint32_t)(my * MAP_WIDTH + mx));
            if (id == 0) continue;

            int draw_x = scx + tx * SPR_W;
            int draw_y = scy + ty * SPR_H;

            int sheet_id = id - 1;
            int bx = (sheet_id % SPR_PER_ROW) * SPR_W;
            int by = (sheet_id / SPR_PER_ROW) * SPR_H;

            for (int row = 0; row < SPR_H; row++) {
                for (int col = 0; col < SPR_W; col++) {
                    uint32_t src = g_sprbank
                                 + (uint32_t)((by + row) * SPR_BANK_DIM + (bx + col)) * 2;
                    uint16_t px = peek2(src);
                    if (px != 0x0000) {
                        _pset(draw_x + col, draw_y + row, px);
                    }
                }
            }
        }
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   PALETTE & COLOR UTILITIES
   ═══════════════════════════════════════════════════════════════ */

/* rgb(r, g, b) — packs raw channels into single 16-bit RGB565 integer */
static int l_rgb(lua_State *L) {
    uint8_t r = (uint8_t)luaL_checknumber(L, 1);
    uint8_t g = (uint8_t)luaL_checknumber(L, 2);
    uint8_t b = (uint8_t)luaL_checknumber(L, 3);
    lua_pushinteger(L, _make_rgb565(r, g, b));
    return 1;
}

/* pal([idx, color16]) or pal(idx, r, g, b) — updates or resets color lookup tables */
static int l_pal(lua_State *L) {
    // If no arguments, reset lookup to baseline specifications
    if (lua_gettop(L) == 0) {
        g_palette[0]  = _make_rgb565(23, 25, 27);    // asphalt
        g_palette[1]  = _make_rgb565(40, 35, 123);   // ocean
        g_palette[2]  = _make_rgb565(50, 89, 226);   // afternoon
        g_palette[3]  = _make_rgb565(51, 165, 255);  // neon
        g_palette[4]  = _make_rgb565(10, 75, 77);    // rainforest
        g_palette[5]  = _make_rgb565(114, 203, 37);  // bamboo
        g_palette[6]  = _make_rgb565(255, 196, 56);  // solar
        g_palette[7]  = _make_rgb565(240, 108, 0);   // tangerine
        g_palette[8]  = _make_rgb565(209, 40, 65);   // strawberry
        g_palette[9]  = _make_rgb565(87, 20, 46);    // cherry
        g_palette[10] = _make_rgb565(151, 63, 63);   // soil
        g_palette[11] = _make_rgb565(241, 194, 132); // caucasian
        g_palette[12] = _make_rgb565(229, 93, 172);  // bubblegum
        g_palette[13] = _make_rgb565(241, 240, 238); // white
        g_palette[14] = _make_rgb565(150, 165, 171); // cobblestone
        g_palette[15] = _make_rgb565(88, 108, 121);  // clay
        return 0;
    }

    int idx = (int)luaL_checknumber(L, 1);
    if (idx < 0 || idx >= 16) return 0;

    if (lua_gettop(L) == 2) {
        // Direct value assignment
        g_palette[idx] = (uint16_t)luaL_checknumber(L, 2);
    } else if (lua_gettop(L) >= 4) {
        // Discrete component assignment
        uint8_t r = (uint8_t)luaL_checknumber(L, 2);
        uint8_t g = (uint8_t)luaL_checknumber(L, 3);
        uint8_t b = (uint8_t)luaL_checknumber(L, 4);
        g_palette[idx] = _make_rgb565(r, g, b);
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   SOUND PROCESSING API
   ═══════════════════════════════════════════════════════════════ */

/* sfx(filename, [volume], [channel]) - plays sfx sound from cassette
volume - defaults to 1.0
channel - defaults to 2, limited to 2 and 3
*/
int l_sfx(lua_State *L) {
    const char *filename = luaL_checkstring(L, 1);
    double vol_mult      = luaL_optnumber(L, 2, 1.0);
    int channel          = luaL_optinteger(L, 3, 2);

    int volume = (int)(vol_mult * 255.0);
    if (volume > 255) volume = 255;
    if (volume < 0)   volume = 0;

    FILE *f = fopen(filename, "rb");
    if (!f) {
        lua_pushboolean(L, 0);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size < 44) {
        fclose(f);
        lua_pushboolean(L, 0);
        return 1;
    }

    // Skip the standard 44-byte WAV header directly to raw PCM bytes
    fseek(f, 44, SEEK_SET);
    long data_size = file_size - 44;
    if (data_size > 32768) data_size = 32768; // 32KB hardware limit

    uint32_t audio_ram_dest = ADDR_SNDBUF + (channel * 0x8000);

    // Read payload right into the high system memory space
    fread(&memory[audio_ram_dest], 1, data_size, f);
    fclose(f);

    // Clear remaining buffer allocation space out to neutral center balance silence (128)
    if (data_size < 32768) {
        memset(&memory[audio_ram_dest + data_size], 128, 32768 - data_size);
    }

    // Pack standard big-endian register pairs matching your apu.lua shifts
    memory[CH_VOLUME(channel)]  = volume;
    memory[CH_ADDR_0(channel)]  = (audio_ram_dest >> 16) & 0xFF;
    memory[CH_ADDR_1(channel)]  = (audio_ram_dest >> 8) & 0xFF;
    memory[CH_ADDR_2(channel)]  = audio_ram_dest & 0xFF;
    
    memory[CH_LEN_0(channel)]   = (data_size >> 16) & 0xFF;
    memory[CH_LEN_1(channel)]   = (data_size >> 8) & 0xFF;
    memory[CH_LEN_2(channel)]   = data_size & 0xFF;
    
    memory[CH_LOOP(channel)]    = 0;
    memory[CH_STATUS(channel)]  = 1;
    memory[CH_TRIGGER(channel)] = 1;

    lua_pushboolean(L, 1);
    return 1;
}

// Private closure function for mus.play()
static int l_closure_play(lua_State *L) {
    // Retrieve the secret upvalues bound to this specific function instance
    const char* filename = lua_tostring(L, lua_upvalueindex(1));
    float volume = (float)lua_tonumber(L, lua_upvalueindex(2));
    
    // Direct hardware execute
    spu_play_module(filename, volume);
    return 0;
}

// Private closure function for mus.pause()
static int l_closure_pause(lua_State *L) {
    spu_pause_module();
    return 0;
}

// Private closure function for mus.fade()
static int l_closure_fade(lua_State *L) {
    float target = (float)luaL_checknumber(L, 1);
    int frames = (int)luaL_checkinteger(L, 2);
    
    spu_fade_module(target, frames);
    return 0;
}

static int l_closure_stop(lua_State *L) {
    spu_stop_module();
    return 0;
}

/* module(filename, volume) - loads module into hardware tracker */
int l_module(lua_State *L) {
    // 1. Grab inputs passed from the user script
    const char* filename = luaL_checkstring(L, 1);
    double volume = luaL_optnumber(L, 2, 1.0);

    // 2. Instantiate a brand new, lightweight Lua table onto the stack
    lua_newtable(L); // Table is now at stack index -1

    // 3. Bake and attach the "play" method with its unique upvalues
    lua_pushstring(L, filename);             // Push value to become Upvalue 1
    lua_pushnumber(L, volume);               // Push value to become Upvalue 2
    lua_pushcclosure(L, l_closure_play, 2);  // Binds the 2 values above to this function instance
    lua_setfield(L, -2, "play");             // table.play = closure

    // 4. Attach the "pause" method (no unique upvalues needed)
    lua_pushcfunction(L, l_closure_pause);
    lua_setfield(L, -2, "pause");            // table.pause = function

    // 5. Attach the "fade" method (no unique upvalues needed)
    lua_pushcfunction(L, l_closure_fade);
    lua_setfield(L, -2, "fade");             // table.fade = function
    
    // 6. Attach the "stop" method (no unique upvalues needed)
    lua_pushcfunction(L, l_closure_stop);
    lua_setfield(L, -2, "stop");             // table.stop = function

    // 7. Return the completed table object back to Lua
    return 1; 
}


/* ═══════════════════════════════════════════════════════════════
   FLASHROM UTILITES
   ═══════════════════════════════════════════════════════════════ */
 
// Internal helper to flush RAM contents directly to the OS filesystem
static void flush_cartdata() {
    if (!g_cartdata_active || g_cartdata_path[0] == '\0') return;

    FILE *f = fopen(g_cartdata_path, "wb");
    if (f) {
        fwrite(g_cartdata_ram, sizeof(int32_t), CARTDATA_SLOTS, f);
        fclose(f);
    }
}

/* flash("your_company_or_game_id") -
Opens or creates the directory, loads data into RAM if it exists */
static int l_flash(lua_State *L) {
    const char *id = luaL_checkstring(L, 1);
    
    // 1. Fetch the safe, OS-sanctioned directory path
    char *pref_dir = SDL_GetPrefPath("yellowfeather", "flashrom");
    if (!pref_dir) {
        return luaL_error(L, "Failed to resolve safe storage directory via SDL.");
    }

    snprintf(g_cartdata_path, sizeof(g_cartdata_path), "%s%s.dat", pref_dir, id);
    
    // SDL_GetPrefPath returns a dynamically allocated string; we must free it!
    SDL_free(pref_dir);

    memset(g_cartdata_ram, 0, sizeof(g_cartdata_ram));
    g_cartdata_active = true;

    FILE *f = fopen(g_cartdata_path, "rb");
    if (f) {
        fread(g_cartdata_ram, sizeof(int32_t), CARTDATA_SLOTS, f);
        fclose(f);
    }

    return 0;
}

/* fget(index) - returns integer value */
static int l_fget(lua_State *L) {
    if (!g_cartdata_active) {
        return luaL_error(L, "cartdata() must be called before calling dget().");
    }

    int index = (int)luaL_checkinteger(L, 1);
    if (index < 0 || index >= CARTDATA_SLOTS) {
        return luaL_error(L, "dget index out of bounds (0-%d).", CARTDATA_SLOTS - 1);
    }

    lua_pushinteger(L, g_cartdata_ram[index]);
    return 1;
}

/* fset(index, value) - set value to flash index */
static int l_fset(lua_State *L) {
    if (!g_cartdata_active) {
        return luaL_error(L, "flash() must be called before calling fset().");
    }

    int index = (int)luaL_checkinteger(L, 1);
    int32_t value = (int32_t)luaL_checkinteger(L, 2);

    if (index < 0 || index >= CARTDATA_SLOTS) {
        return luaL_error(L, "fset index out of bounds (0-%d).", CARTDATA_SLOTS - 1);
    }

    // Update value in RAM
    g_cartdata_ram[index] = value;

    // Flush to disk immediately upon mutation to protect against sudden crashes
    flush_cartdata(); 

    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   SHORTHAND AMENITIES
   ═══════════════════════════════════════════════════════════════ */
 
static int l_clamp(lua_State *L) {
    double val = luaL_checknumber(L, 1);
    double lo  = luaL_checknumber(L, 2);
    double hi  = luaL_checknumber(L, 3);
    lua_pushnumber(L, val < lo ? lo : val > hi ? hi : val);
    return 1;
}
 
static int l_sin  (lua_State *L) { lua_pushnumber(L, sin  (luaL_checknumber(L, 1))); return 1; }
static int l_cos  (lua_State *L) { lua_pushnumber(L, cos  (luaL_checknumber(L, 1))); return 1; }
static int l_tan  (lua_State *L) { lua_pushnumber(L, tan  (luaL_checknumber(L, 1))); return 1; }
static int l_sqrt (lua_State *L) { lua_pushnumber(L, sqrt (luaL_checknumber(L, 1))); return 1; }
static int l_flr  (lua_State *L) { lua_pushnumber(L, floor(luaL_checknumber(L, 1))); return 1; }
static int l_ceil (lua_State *L) { lua_pushnumber(L, ceil (luaL_checknumber(L, 1))); return 1; }
 
static int l_min(lua_State *L) {
    double a = luaL_checknumber(L, 1);
    double b = luaL_checknumber(L, 2);
    lua_pushnumber(L, a < b ? a : b);
    return 1;
}
 
static int l_max(lua_State *L) {
    double a = luaL_checknumber(L, 1);
    double b = luaL_checknumber(L, 2);
    lua_pushnumber(L, a > b ? a : b);
    return 1;
}
 
static int l_mid(lua_State *L) {
    double x = luaL_checknumber(L, 1);
    double y = luaL_checknumber(L, 2);
    double z = luaL_checknumber(L, 3);
    double lo = x < y ? x : y;
    double hi = x > y ? x : y;
    double mid = z < lo ? lo : z > hi ? hi : z;
    lua_pushnumber(L, mid);
    return 1;
}
 
static int l_add(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getglobal(L, "table");
    lua_getfield(L, -1, "insert");
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_call(L, 2, 0);
    lua_pop(L, 1);
    return 0;
}
 
static int l_t(lua_State *L) {
    lua_pushnumber(L, (double)clock() / CLOCKS_PER_SEC);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════
   REGISTRATION
   ═══════════════════════════════════════════════════════════════ */

static const luaL_Reg api[] = {
    /* graphics rendering primitives */
    { "cls",       l_cls       },
    { "pset",      l_pset      },
    { "pget",      l_pget      },
    { "line",      l_line      },
    { "rect",      l_rect      },
    { "rectfill",  l_rectfill  },
    { "circ",      l_circ      },
    { "circfill",  l_circfill  },
    { "camera",    l_camera    },
    { "clip",      l_clip      },
    
    /* color lookup & conversions */
    { "rgb",       l_rgb       },
    { "pal",       l_pal       },
    
    /* audio api */
    { "sfx",       l_sfx       },
    { "module",    l_module    },

    /* assets & fonts */
    { "sbank",     l_sbank     },
    { "spr",       l_spr       },
    { "sprsht",    l_sprsht    },
    { "text",      l_text      },

    /* controls/inputs */
    { "btn",       l_btn       },
    { "btnp",      l_btnp      },

    /* matrix tilemap functions */
    { "tile",      l_tile      },
    { "mget",      l_mget      },
    { "mset",      l_mset      },
    { "map",       l_map       },
    
    /* flashrom handling */
    { "flash",     l_flash     },
    { "fget",      l_fget      },
    { "fset",      l_fset      },

    /* math amenities */
    { "clamp",     l_clamp     },
    { "sin",       l_sin       },
    { "cos",       l_cos       },
    { "tan",       l_tan       },
    { "sqrt",      l_sqrt      },
    { "flr",       l_flr       },
    { "ceil",      l_ceil      },
    { "min",       l_min       },
    { "max",       l_max       },
    { "mid",       l_mid       },
    { "add",       l_add       },
    { "t",         l_t         },
    { NULL, NULL }
};

void api_register(lua_State *L) {
    init_font_map();
    /* Auto-initialize baseline system palette configuration layout */
    lua_pushcfunction(L, l_pal);
    lua_call(L, 0, 0);

    /* register all functions into global context space _G */
    lua_getglobal(L, "_G");
    luaL_setfuncs(L, api, 0);
    lua_pop(L, 1);
    
    /* expose memory map constants so carts can peek/poke by name */
#define SETK(name, val) \
    lua_pushinteger(L, (lua_Integer)(val)); lua_setglobal(L, name)

    SETK("ADDR_FB",    ADDR_FB);
    SETK("ADDR_INPUT", ADDR_INPUT);
    SETK("ADDR_AUDIO", ADDR_AUDIO);
    SETK("ADDR_FONT",  ADDR_FONT);
    SETK("ADDR_SPRB0", ADDR_SPRB0);
    SETK("ADDR_SPRB1", ADDR_SPRB1);
    SETK("ADDR_MAP",   ADDR_MAP);
    SETK("ADDR_CART",  ADDR_CART);

    /* button index constants mapping bits */
    SETK("BTN_LEFT",  0);
    SETK("BTN_RIGHT", 1);
    SETK("BTN_UP",    2);
    SETK("BTN_DOWN",  3);
    SETK("BTN_A",     4);
    SETK("BTN_B",     5);
    SETK("BTN_C",     6);
    SETK("BTN_D",     7);
    SETK("BTN_START", 8);

#undef SETK
}
