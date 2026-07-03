/*
 * api.c — Yellow Feather Core Engine C API
 * Complete 1:1 implementation from the original LUA API
 */

#include "api.h"
#include "mem.h"
#include "audio.h"

/* ── custom font lookup map ──────────────────────────────────── */
static int g_ascii_to_font_index[256];
static int g_font_map_initialized = 0;

/* ═══════════════════════════════════════════════════════════════
   INTERNAL PIXEL & COLOR HELPERS
   ═══════════════════════════════════════════════════════════════ */

/* Indexes all 512 colors in ADDR_PAL */
static inline uint16_t _resolve_color(int col) {
    if (col >= 0 && col < 512) {
        uint32_t addr = ADDR_PAL + (col * 2);
        return (uint16_t)(memory[addr] | (memory[addr + 1] << 8));
    }
    return (uint16_t)col;
}

/* Natively handles camera transforms, clip windows, and memory-poking */
static inline void _pixel(int x, int y, int col) {

    int16_t cam_x = (int16_t)peek2(REG_CAM_X);
    int16_t cam_y = (int16_t)peek2(REG_CAM_Y);

    x -= cam_x;
    y -= cam_y;

    if (peek(REG_CLIP_EN) == 1) {
        if (x < peek(REG_CLIP_X0) || x > peek(REG_CLIP_X1) ||
            y < peek(REG_CLIP_Y0) || y > peek(REG_CLIP_Y1)) {
            return; // Hardware Clip Discarded
        }
    }

    if (x < 0 || x >= FB_WID || y < 0 || y >= FB_HEI) return;
    
    uint16_t color16 = _resolve_color(col);
    
        // 1. Read the 16-bit pattern mask from hardware registers
    uint16_t pattern = (memory[REG_FILLP] << 8) | memory[REG_FILLP + 1];

    if (pattern != 0) {
        // Find which bit of the 4x4 grid we are currently on
        int bit_index = (x % 4) + ((y % 4) * 4);
        
        // Check if the bit is active (reading from MSB to LSB)
        if ((pattern >> (15 - bit_index)) & 1) {
            uint8_t secondary_color = memory[REG_FILLP_COLOR];
            
            // Treat color index 0 (or any choice) as transparent dither
            if (secondary_color == 0xFF) return; 
            color16 = secondary_color;
        }
    }
    
    uint32_t addr = ADDR_FB + (uint32_t)(y * FB_WID + x) * 2;
    poke2(addr, color16);
}

static void init_pal(void) {
    const uint8_t g_palette[16][3] = {
        {23, 25, 27},     // asphalt
        {40, 35, 123},    // ocean
        {50, 89, 226},    // afternoon
        {51, 165, 255},   // neon
        {10, 75, 77},     // rainforest
        {114, 203, 37},   // bamboo
        {255, 196, 56},   // solar
        {240, 108, 0},    // tangerine
        {209, 40, 65},    // strawberry
        {87, 20, 46},     // cherry
        {151, 63, 63},    // soil
        {241, 194, 132},  // caucasian
        {229, 93, 172},   // bubblegum
        {241, 240, 238},  // white
        {150, 165, 171},  // cobblestone
        {88, 108, 121}    // clay
    };

    // Commit them straight to the Palette RAM address sector
    for (int i = 0; i < 16; i++) {
        uint16_t color16 = RGB_CONVERT(g_palette[i][0], g_palette[i][1], g_palette[i][2]);
        uint32_t target_addr = ADDR_PAL + (i * 2);
        
        // Write unmanaged bytes: Little-Endian storage layout
        memory[target_addr]     = (uint8_t)(color16 & 0xFF);        // Low byte
        memory[target_addr + 1] = (uint8_t)((color16 >> 8) & 0xFF); // High byte
    }
}

static void map_font(void) {
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

/* ═══════════════════════════════════════════════════════════════
   MEMORY API
   ═══════════════════════════════════════════════════════════════ */

static int l_peek(lua_State *L) {
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    int size = (int)luaL_optinteger(L, 2, 1);
    uint32_t val;
    if (size == 2)      val = peek2(addr);
    else if (size == 4) val = peek4(addr);
    else                val = peek(addr);
    lua_pushinteger(L, val);
    return 1;
}

static int l_poke(lua_State *L) {
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    lua_Integer val = luaL_checkinteger(L, 2);
    int size = (int)luaL_optinteger(L, 3, 0);
    if (size == 2)      poke2(addr, (uint16_t)val);
    else if (size == 4) poke4(addr, (uint32_t)val);
    else if (size == 1) poke(addr, (uint8_t)val);
    else {
        if (val < 0 || val > 65535) poke4(addr, (uint32_t)val);
        else if (val > 255)         poke2(addr, (uint16_t)val);
        else                        poke(addr, (uint8_t)val);
    }
    return 0;
}

static int l_memset(lua_State *L) {
    uint32_t dest  = (uint32_t)luaL_checknumber(L, 1);
    uint8_t  val   = (uint8_t)luaL_checknumber(L, 2);
    uint32_t count = (uint32_t)luaL_checknumber(L, 3);

    if (dest + count <= RAM_SIZE) {
        memset(memory + dest, val, count);
    }
    return 0;
}

static int l_memcpy(lua_State *L) {
    // Argument 1 is always the target address in your fantasy console RAM
    uint32_t dest_addr = (uint32_t)luaL_checkinteger(L, 1);

    // Check the type of the second argument dynamically
    if (lua_type(L, 2) == LUA_TSTRING) {
        // SCENARIO A: The user passed a raw Lua string!
        size_t str_len;
        const char *src_str = luaL_checklstring(L, 2, &str_len);

        // If argument 3 is provided, use it as count; otherwise default to full string length
        size_t count = luaL_optinteger(L, 3, str_len);
        
        // Stream bytes straight from the Lua VM heap into your hardware memory array
        memcpy(&memory[dest_addr], src_str, count);

    } else {
        // SCENARIO B: The user passed a source memory address number
        uint32_t src_addr = (uint32_t)luaL_checkinteger(L, 2);
        size_t count      = (size_t)luaL_checkinteger(L, 3);

        // Standard hardware-to-hardware RAM block copy
        memcpy(&memory[dest_addr], &memory[src_addr], count);
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   GRAPHICS API
   ═══════════════════════════════════════════════════════════════ */

/* clear([col]) — clear screen to color index or raw RGB565 color */
static int l_clear(lua_State *L) {
    int col_param = (int)luaL_optinteger(L, 1, 0);
    uint16_t color16 = _resolve_color(col_param);

    // Clear whole frame buffer sequentially via standard poke2 iterations
    for (uint32_t addr = ADDR_FB; addr < ADDR_FB + (FB_WID * FB_HEI * 2); addr += 2) {
        poke2(addr, color16);
    }
    return 0;
}

/* pixel(x, y, col) */
static int l_pixel(lua_State *L) {
    _pixel((int)luaL_checknumber(L, 1),
          (int)luaL_checknumber(L, 2),
          (int)luaL_checknumber(L, 3));
    return 0;
}

/* rect(x, y, w, h, col) - Fast DMA-style filled rectangle blit to VRAM */
static int l_rect(lua_State *L) {
    // 1. Accept numbers for transparent sub-pixel processing
    int x = (int)luaL_checknumber(L, 1);
    int y = (int)luaL_checknumber(L, 2);
    int w = (int)luaL_checknumber(L, 3);
    int h = (int)luaL_checknumber(L, 4);
    int col = (int)luaL_checkinteger(L, 5);

    // 1. Fetch live transformations from Hardware Registers
    int16_t cam_x = (int16_t)peek2(REG_CAM_X);
    int16_t cam_y = (int16_t)peek2(REG_CAM_Y);

    x -= cam_x;
    y -= cam_y;

    // Early exit for invisible dimensions
    if (w <= 0 || h <= 0) return 0;

    // 2. Define coordinate boundaries
    int x1 = x;
    int y1 = y;
    int x2 = x + w;
    int y2 = y + h;

    // 3. Hardware Clipping: Clamp boundaries strictly within the Frame Buffer grid
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > FB_WID) x2 = FB_WID;
    if (y2 > FB_HEI) y2 = FB_HEI;

    // 2. Fetch Scissors Engine configuration settings
    uint8_t clip_en  = peek(REG_CLIP_EN);
    int min_x = 0;
    int min_y = 0;
    int max_x = FB_WID - 1;
    int max_y = FB_HEI - 1;

    if (clip_en == 1) {
        min_x = (int)peek(REG_CLIP_X0);
        min_y = (int)peek(REG_CLIP_Y0);
        max_x = (int)peek(REG_CLIP_X1);
        max_y = (int)peek(REG_CLIP_Y1);
    }

    // Early exit if the rectangle is entirely off-screen
    if (x1 >= x2 || y1 >= y2) return 0;


    // 4. Resolve color to a single 16-bit word once
    uint16_t color = _resolve_color(col);
    uint8_t  low_byte  = (uint8_t)(color & 0xFF);
    uint8_t  high_byte = (uint8_t)((color >> 8) & 0xFF);

    uint8_t *fb = &memory[ADDR_FB];
    int row_pixels = x2 - x1;

    // 5. Blast data row-by-row into the unmanaged memory map
    for (int cy = y1; cy < y2; cy++) {
        if (y1 < min_y || y1 > max_y) continue;
        // if (y2 < min_y || y2 > max_y) continue; // because of this and x2 i dont know for sure if rect will obey clipping
        if (x1 < min_x || x1 > max_x) continue;
        // if (x2 < min_x || x2 > max_x) continue;
        // Calculate the physical byte start offset for this specific row line
        uint32_t row_bytes_offset = (cy * FB_WID + x1) * 2;

        for (int i = 0; i < row_pixels; i++) {
            uint32_t pixel_offset = row_bytes_offset + (i * 2);
            fb[pixel_offset]     = low_byte;
            fb[pixel_offset + 1] = high_byte;
        }
    }

    return 0;
}

/* camera([x, y]) — apply global hardware camera view displacement */
static int l_camera(lua_State *L) {
    int16_t x = (int16_t)luaL_optinteger(L, 1, 0);
    int16_t y = (int16_t)luaL_optinteger(L, 2, 0);

    // Write straight into virtual physical memory addresses
    poke2(REG_CAM_X, x);
    poke2(REG_CAM_Y, y);

    return 0;
}

/* clip([x, y, w, h]) — applies global rendering boundaries */
static int l_clip(lua_State *L) {
    if (lua_gettop(L) == 0) {
        // Disabling clipping mirrors turning off a hardware flag register
        poke(REG_CLIP_EN, 0);
    } else {
        uint8_t x = (uint8_t)luaL_checkinteger(L, 1);
        uint8_t y = (uint8_t)luaL_checkinteger(L, 2);
        uint8_t w = (uint8_t)luaL_checkinteger(L, 3);
        uint8_t h = (uint8_t)luaL_checkinteger(L, 4);

        poke(REG_CLIP_EN, 1);
        poke(REG_CLIP_X0, x);
        poke(REG_CLIP_Y0, y);
        poke(REG_CLIP_X1, x + w - 1);
        poke(REG_CLIP_Y1, y + h - 1);
    }
    return 0;
}

// Track spreadsheet dimensions directly inside the native runtime layer
static uint32_t bank_addresses[] = { 0x06900, 0x0A900 };
static int bank_widths[]         = { 128, 128 };
static int bank_heights[]        = { 64, 64 };

/* sprite(id, x, y, [w], [h], [flip_x], [flip_y]) */
int l_sprite(lua_State *L) {
    int id       = (int)luaL_checknumber(L, 1);
    int screen_x = (int)luaL_checknumber(L, 2);
    int screen_y = (int)luaL_checknumber(L, 3);
    int w        = (int)luaL_optnumber(L, 4, 1);
    int h        = (int)luaL_optnumber(L, 5, 1);
    bool flip_x  = lua_toboolean(L, 6);
    bool flip_y  = lua_toboolean(L, 7);

    int16_t cam_x = (int16_t)peek2(REG_CAM_X);
    int16_t cam_y = (int16_t)peek2(REG_CAM_Y);

    screen_x -= cam_x;
    screen_y -= cam_y;

    uint8_t clip_en = peek(REG_CLIP_EN);
    int min_x = 0, min_y = 0;
    int max_x = FB_WID - 1, max_y = FB_HEI - 1;

    if (clip_en == 1) {
        min_x = (int)peek(REG_CLIP_X0);
        min_y = (int)peek(REG_CLIP_Y0);
        max_x = (int)peek(REG_CLIP_X1);
        max_y = (int)peek(REG_CLIP_Y1);
    }

    uint8_t current_bank = memory[REG_BANK_SW];
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
        if (py < min_y || py > max_y) continue;
        for (int px = 0; px < total_w; px++) {
            int dest_x = screen_x + px;
            int dest_y = screen_y + py;
            if (dest_x < 0 || dest_x >= 128 || dest_y < 0 || dest_y >= 96) continue;
            if (px < min_x || px > max_x) continue;

            int target_src_x = flip_x ? (total_w - 1 - px) : px;
            int target_src_y = flip_y ? (total_h - 1 - py) : py;

            int source_x = spr_x + target_src_x;
            int source_y = spr_y + target_src_y;

            if (source_x < 0 || source_x >= sheet_width) continue;

            uint32_t src_addr = sheet_base + (source_y * sheet_width + source_x);
            uint8_t color_index = memory[src_addr];

            // 0 is transparent. Anything else gets shifted back by 1 for the hardware palette!
            if (color_index != 0) {
                uint16_t color16 = _resolve_color(color_index - 1);

                uint32_t fb_addr = ADDR_FB + ((dest_y * 128 + dest_x) * 2);
                memory[fb_addr]     = color16 & 0xFF;
                memory[fb_addr + 1] = (color16 >> 8) & 0xFF;
            }
        }
    }
    return 0;
}

/* text(str, x, y, [col]) 
   16-Bit Proportional Pro-Text Printer */
static int l_text(lua_State *L) {
    const char *str = luaL_checkstring(L, 1);
    int start_x     = (int)luaL_checknumber(L, 2);
    int y           = (int)luaL_checknumber(L, 3);
    /* Default to palette's white color (0xEF7D) if no argument is provided */
    uint16_t color  = (uint16_t)luaL_optinteger(L, 4, 0xEF7D);

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
                            _pixel(x + col, y + row, color);
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
   TILEMAP API
   ═══════════════════════════════════════════════════════════════ */

/* tile(x, y, [id]) fetches or sets tile id in the tilemap block */
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

/* experimental map functoin implementing layers */
static int l_map(lua_State *L) {
    int stx = (int)luaL_checknumber(L, 1);
    int sty = (int)luaL_checknumber(L, 2);
    int scx = (int)luaL_checknumber(L, 3);
    int scy = (int)luaL_checknumber(L, 4);
    int tw  = (int)luaL_optinteger(L, 5, FB_WID / SPR_W);
    int th  = (int)luaL_optinteger(L, 6, FB_HEI / SPR_H);
    
    int layer = (int)luaL_optinteger(L, 7, 0);

    uint8_t current_bank = memory[REG_BANK_SW];
    if (current_bank > 1) current_bank = 0;

    uint32_t sheet_base  = bank_addresses[current_bank];
    int      sheet_width = bank_widths[current_bank];
    int      sheet_cols  = sheet_width / SPR_W;
    if (sheet_cols <= 0) return 0;

    // calculate the memory offset for this specific layer stack
    uint32_t layer_size = (uint32_t)(MAP_WIDTH * MAP_HEIGHT);
    uint32_t layer_offset = (uint32_t)layer * layer_size;

    for (int ty = 0; ty < th; ty++) {
        int my = sty + ty;
        if (my < 0 || my >= MAP_HEIGHT) continue;

        for (int tx = 0; tx < tw; tx++) {
            int mx = stx + tx;
            if (mx < 0 || mx >= MAP_WIDTH) continue;

            // inject the layer offset directly into your flat RAM reading address
            uint8_t id = peek(ADDR_MAP + layer_offset + (uint32_t)(my * MAP_WIDTH + mx));
            if (id == 0) continue;

            int draw_x = scx + tx * SPR_W;
            int draw_y = scy + ty * SPR_H;

            int sheet_id = id - 1;
            int bx = (sheet_id % sheet_cols) * SPR_W;
            int by = (sheet_id / sheet_cols) * SPR_H;

            for (int row = 0; row < SPR_H; row++) {
                for (int col = 0; col < SPR_W; col++) {
                    uint32_t src = sheet_base + (uint32_t)((by + row) * sheet_width + (bx + col));
                    uint8_t color_index = peek(src);
                    if (color_index != 0) {
                        _pixel(draw_x + col, draw_y + row, color_index - 1);
                    }
                }
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
    int btn_idx = (int)luaL_checkinteger(L, 1);

    // Combine player offset to index the global 32-bit block
    int absolute_bit = btn_idx;

    // Read the live byte block where this bit resides
    uint32_t live_mask = peek4(ADDR_INPUT);

    lua_pushboolean(L, (live_mask & (1 << absolute_bit)) != 0);
    return 1;
}

/* btnp(n) → bool */
static int l_btnp(lua_State *L) {
    int btn_idx = (int)luaL_checkinteger(L, 1);

    int absolute_bit = btn_idx;

    uint32_t live_mask = peek4(ADDR_INPUT);
    uint32_t prev_mask = peek4(ADDR_INPUT + 4);

    // Button is pressed now, but WAS NOT pressed on the previous frame loop
    bool pressed = ((live_mask & (1 << absolute_bit)) != 0) && 
                   ((prev_mask & (1 << absolute_bit)) == 0);

    lua_pushboolean(L, pressed);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════
   AUDIO PROCESSING API
   ═══════════════════════════════════════════════════════════════ */

/* sound(slot, [volume], [channel], [pitch])
 * Lua syntax examples: 
 * sound(5)             -- Plays slot 5 on an auto-allocated SFX channel
 * sound(12, 128, 2)    -- Plays slot 12 on channel 2 at half volume
 */
static int l_sound(lua_State *L) {
    int slot = luaL_checkinteger(L, 1);
    if (slot < 0 || slot >= 64) {
        return luaL_error(L, "Invalid sound slot index %d (Must be 0-63)", slot);
    }

    // Read optional volume (default: 255)
    int volume = (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) ? luaL_checkinteger(L, 2) : 255;
  
    // Default to automatic channel picking between CHAN_SFX_1 (2) and CHAN_SFX_2 (3)
    // if the user doesn't pass an explicit channel index argument
    int ch = CHAN_SFX_1; 
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
        ch = luaL_checkinteger(L, 3);
        if (ch < 0 || ch >= AUDIO_CHANNELS) {
            return luaL_error(L, "Invalid hardware voice channel %d (Must be 0-3)", ch);
        }
    } else {
        // Simple alternate allocation toggle: if Channel 2 is busy, use Channel 3
        if (memory[CH_STATUS(CHAN_SFX_1)] != 0 && memory[CH_STATUS(CHAN_SFX_2)] == 0) {
            ch = CHAN_SFX_2;
        }
    }

    // pitch (default: 256 -> 1.0 baseline)
    int pitch  = (lua_gettop(L) >= 4 && !lua_isnil(L, 4)) ? luaL_checkinteger(L, 4) : 256;

    // Bound check parameters
    if (volume < 0) volume = 0; if (volume > 255) volume = 255;
    if (pitch < 0)  pitch = 0;

    // --- FETCH SOUND METADATA HEADER FROM RAM ---
    // Header entry layout size is exactly 12 bytes
    uint32_t header_ptr = ADDR_SNDBNK + (slot * 12);

    // Reconstruct fields safely using 32-bit and 16-bit type casting
    uint32_t sample_offset = *(uint32_t*)&memory[header_ptr + 0];
    uint32_t sample_length = *(uint32_t*)&memory[header_ptr + 4];
    uint16_t loop_point    = *(uint16_t*)&memory[header_ptr + 8];
    uint8_t  flags         = memory[header_ptr + 11]; // Flags byte moved to offset 11

    // If length is zero, no asset exists in this slot!
    if (sample_length == 0) {
        return 0; 
    }

    // --- POKE DATA TO AUDIO REGISTERS ---
    // Stop the channel processing momentarily to change values cleanly
    memory[CH_STATUS(ch)] = 0;

    // Write address pointers and lengths (Note: your current CH_ADDR/LEN registers 
    // are 16-bit, which safely fits up to a 64KB soundbank boundary)
    memory[CH_ADDR_LO(ch)] = sample_offset & 0xFF;
    memory[CH_ADDR_HI(ch)] = (sample_offset >> 8) & 0xFF;
    memory[CH_LEN_LO(ch)]  = sample_length & 0xFF;
    memory[CH_LEN_HI(ch)]  = (sample_length >> 8) & 0xFF;

    // Check Bit 0 of our compiled flag byte to set hardware loop register
    memory[CH_LOOP(ch)]    = (flags & 0x01);

    // Commit volume and pitch settings
    memory[CH_VOLUME(ch)]  = (uint8_t)volume;
    memory[CH_PITCH(ch)]   = (uint8_t)pitch; 

    // Activate the runtime decompression flag matching the asset type
    uint8_t run_mode = (flags & 0x02) ? 2 : 1;
    memory[CH_STATUS(ch)] = run_mode;

    // Slam the hardware trigger high!
    memory[CH_TRIGGER(ch)] = 1;
    return 0;
}

// closure function
static int l_closure_play(lua_State *L) {
    spu_play_module();
    return 0;
}

// closure function
static int l_closure_pause(lua_State *L) {
    spu_pause_module();
    return 0;
}

// closure function
static int l_closure_fade(lua_State *L) {
    float target = (float)luaL_checknumber(L, 1);
    int frames = (int)luaL_checkinteger(L, 2);
    
    spu_fade_module(target, frames);
    return 0;
}

// closure function
static int l_closure_stop(lua_State *L) {
    spu_stop_module();
    return 0;
}

/* module(filename, volume) - api for hardware tracker */
int l_module(lua_State *L) {
    const char* filename = luaL_checkstring(L, 1);
    double volume = luaL_optnumber(L, 2, 1.0);
    
    spu_start_module(filename, volume);

    lua_newtable(L); // Table is now at stack index -1
    
    lua_pushcfunction(L, l_closure_play);
    lua_setfield(L, -2, "play");             // table.play = closure

    lua_pushcfunction(L, l_closure_pause);
    lua_setfield(L, -2, "pause");            // table.pause = function

    lua_pushcfunction(L, l_closure_fade);
    lua_setfield(L, -2, "fade");             // table.fade = function
    
    lua_pushcfunction(L, l_closure_stop);
    lua_setfield(L, -2, "stop");             // table.stop = function

    return 1; 
}

/* ═══════════════════════════════════════════════════════════════
   REGISTRATION
   ═══════════════════════════════════════════════════════════════ */

static const luaL_Reg api[] = {
    /* basic memory operations */
    { "peek",      l_peek      },
    { "poke",      l_poke      },
    
    /* batch memory operations */
    { "memset",    l_memset    },
    { "memcpy",    l_memcpy    },
    
    /* rendering primitives */
    { "clear",     l_clear     },
    { "pixel",     l_pixel     },
    { "rect",      l_rect      },
    { "camera",    l_camera    },
    { "clip",      l_clip      },

    /* audio api */
    { "sound",     l_sound     },
    { "module",    l_module    },

    /* assets & fonts */
    { "sprite",    l_sprite    },
    { "text",      l_text      },

    /* controls/inputs */
    { "btn",       l_btn       },
    { "btnp",      l_btnp      },

    /* matrix tilemap functions */
    { "tile",      l_tile      },
    { "map",       l_map       },
    { NULL, NULL }
};

void api_register(lua_State *L) {
    map_font();
    init_pal();

    /* register all functions into global context space _G */
    lua_getglobal(L, "_G");
    luaL_setfuncs(L, api, 0);
    lua_pop(L, 1);

}
