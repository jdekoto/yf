/*
 * api.h — Yellow Feather C API header
 */

#ifndef API_H
#define API_H

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <errno.h>
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

/* ── tilemap constants ───────────────────────────────────────── */
#define MAP_WIDTH      512
#define MAP_HEIGHT     256
/* ── save data handling ──────────────────────────────────────── */
 #define SRAM_SIZE 8192 // 8KB hard allocation limit

/* ── native engine 16-color palette storage ──────────────────── */
/* ── 16-bit color packing formula ────────────────────────────── */
// Standard RGB565 packing (5 bits Red, 6 bits Green, 5 bits Blue)
#define RGB_CONVERT(r, g, b) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

/* call once in vm_init() after luaL_openlibs()
   registers all graphics, input, and tilemap functions into _G
   along with ADDR_* and BTN_* constants. */
void api_register(lua_State *L);

#endif
