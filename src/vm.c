#include "vm.h"
#include "mem.h"
#include "audio.h"
#include "api.h"
#include "runtime.h"
#include <string.h>
#include <dirent.h>

/* vm.h */
#define VM_OPS_PER_FRAME  100000  /* ~4.5M insts / sec i think */


/* ── memory API ──────────────────────────────────────────────── */

static int l_peek (lua_State *L) { lua_pushinteger(L, peek ((uint32_t)luaL_checkinteger(L,1)));              return 1; }
static int l_poke (lua_State *L) { poke((uint32_t)luaL_checkinteger(L,1),(uint8_t) luaL_checkinteger(L,2));  return 0; }
static int l_peek2(lua_State *L) { lua_pushinteger(L, peek2((uint32_t)luaL_checkinteger(L,1)));              return 1; }
static int l_poke2(lua_State *L) { poke2((uint32_t)luaL_checkinteger(L,1),(uint16_t)luaL_checkinteger(L,2)); return 0; }
static int l_peek4(lua_State *L) { lua_pushinteger(L, peek4((uint32_t)luaL_checkinteger(L,1)));              return 1; }
static int l_poke4(lua_State *L) { poke4((uint32_t)luaL_checkinteger(L,1),(uint32_t)luaL_checkinteger(L,2)); return 0; }

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
    uint32_t dest  = (uint32_t)luaL_checknumber(L, 1);
    uint32_t src   = (uint32_t)luaL_checknumber(L, 2);
    uint32_t count = (uint32_t)luaL_checknumber(L, 3);

    if (dest + count <= RAM_SIZE && src + count <= RAM_SIZE) {
        memmove(memory + dest, memory + src, count);
    }
    return 0;
}

// Usage in Lua: reload_file_c("assets/level2.map", dest_memory_address)
static int l_reload(lua_State *L) {
    const char* filename = luaL_checkstring(L, 1);
    uint32_t dest_addr   = (uint32_t)luaL_checknumber(L, 2);

    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("[Engine ERROR] Could not open file: \"%s\". Check your path!\n", filename);
        lua_pushboolean(L, 0); // Return false to Lua
        return 1;
    }

    // Find out how big the file is
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Safety check to ensure we don't overflow the system RAM limits
    if (dest_addr + size <= RAM_SIZE) {
        size_t read_bytes = fread(memory + dest_addr, 1, size, f);
        lua_pushboolean(L, 1); // Return true to Lua
    } else {
        printf("[Engine ERROR] Out of bounds! Address 0x%X + Size %ld exceeds RAM_SIZE\n", dest_addr, size);
        lua_pushboolean(L, 0); // Return false to Lua
    }

    fclose(f);
    return 1;
}

// Usage in Lua: cstore_file_c("assets/savegame.dat", src_memory_address, byte_count)
int l_cstore(lua_State *L) {
    const char* filename = luaL_checkstring(L, 1);
    uint32_t src_addr    = (uint32_t)luaL_checknumber(L, 2);
    uint32_t count       = (uint32_t)luaL_checknumber(L, 3);

    if (src_addr + count <= RAM_SIZE) {
        FILE *f = fopen(filename, "wb");
        if (f) {
            fwrite(memory + src_addr, 1, count, f);
            fclose(f);
        }
    }
    return 0;
}

/* ── hardware tracker API ──────────────────────────────────────────────── */

int l_feedtracker(lua_State *L) {
  const char* filename = luaL_checkstring(L, 1);
  
  spu_feedtracker(filename);
  
  return 0;
}


/* ── vm ──────────────────────────────────────────────────────── */

static const luaL_Reg api[] = {
    {"peek",          l_peek},  
    {"poke",          l_poke},
    {"peek2",         l_peek2}, 
    {"poke2",         l_poke2},
    {"peek4",         l_peek4}, 
    {"poke4",         l_poke4},
    {"memset",        l_memset},
    {"memcpy",        l_memcpy},  
    {"reload",        l_reload},
    {"cstore",        l_cstore},
    {"feed_tracker",  l_feedtracker},
    {NULL, NULL},
};

static void call_fn(lua_State *L, const char *name) {
    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "%s: %s\n", name, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// simple hot reload for boot.lua and sources
static time_t last_sources_mtime = 0;
static time_t last_mtime = 0;

static bool cart_changed(const char *path) {
    struct stat s;
    if (stat(path, &s) != 0) return false;
    if (s.st_mtime != last_mtime) {
        last_mtime = s.st_mtime;
        return last_mtime != 0;   /* skip first frame */
    }
    return false;
}

// i know it looks exactly the same but i promised it prevents a segfault
static bool folder_changed(const char *path, time_t *tracked_time) {
    struct stat s;
    if (stat(path, &s) != 0) return false;
    if (s.st_mtime != *tracked_time) {
        *tracked_time = s.st_mtime;
        return *tracked_time != 0;   /* Skip the first initialization frame */
    }
    return false;
}

void vm_reload(VM *vm, const char *path) {
    bool boot_changed = cart_changed(path);
    bool modules_changed = folder_changed("sources", &last_sources_mtime);

    // If EITHER has a legitimate structural modification, trigger the reboot
    if (boot_changed || modules_changed) {
        vm_shutdown(vm);  
        vm_init(vm);      
        vm_load(vm, path);
    }
}

// so you dont have to woory about it + plus wont bloat your cassette
void vm_runtime(VM *vm) {
    if (luaL_dostring(vm->L, EYECANDY_SOURCE) != LUA_OK) {
        fprintf(stderr, "Failed to inject embedded graphics runtime: %s\n", lua_tostring(vm->L, -1));
        lua_pop(vm->L, 1);
    }
    if (luaL_dostring(vm->L, APU_SOURCE) != LUA_OK) {
        fprintf(stderr, "Failed to inject embedded audio runtime: %s\n", lua_tostring(vm->L, -1));
        lua_pop(vm->L, 1);
    }
    if (luaL_dostring(vm->L, JOYPADS_SOURCE) != LUA_OK) {
        fprintf(stderr, "Failed to inject embedded input runtime: %s\n", lua_tostring(vm->L, -1));
        lua_pop(vm->L, 1);
    }
    if (luaL_dostring(vm->L, BATUTTA_SOURCE) != LUA_OK) {
        fprintf(stderr, "Failed to inject embedded tilemap runtime: %s\n", lua_tostring(vm->L, -1));
        lua_pop(vm->L, 1);
    }
    if (luaL_dostring(vm->L, SHORTHAND_SOURCE) != LUA_OK) {
        fprintf(stderr, "Failed to inject embedded shorthand runtime: %s\n", lua_tostring(vm->L, -1));
        lua_pop(vm->L, 1);
    }
}

static int  g_ops       = 0;
static bool g_cpu_limit = false;

static void cpu_hook(lua_State *L, lua_Debug *ar) {
    (void)ar;
    g_ops += 100;   /* hook fires every 100 ops */
    if (g_ops >= VM_OPS_PER_FRAME) {
        g_cpu_limit = true;
        luaL_error(L, "MAXIMUM CPU LEVEL REACHED");
    }
}

void vm_init(VM *vm) {
    vm->L = luaL_newstate();
    luaL_openlibs(vm->L);
    lua_sethook(vm->L, cpu_hook, LUA_MASKCOUNT, 100);

    /* expose API globally */
    lua_getglobal(vm->L, "_G");
    luaL_setfuncs(vm->L, api, 0);
    lua_pop(vm->L, 1);
    
    api_register(vm->L);
}

void vm_load(VM *vm, const char *path) {
    
  
    if (luaL_dofile(vm->L, path) != LUA_OK) {
        fprintf(stderr, "vm_load: %s\n", lua_tostring(vm->L, -1));
        lua_pop(vm->L, 1);
    }
    
    call_fn(vm->L, "_boot");
}

void vm_update  (VM *vm) { 
    g_ops       = 0;
    g_cpu_limit = false;
    call_fn(vm->L, "_tick"); 
}
void vm_shutdown(VM *vm) { 
    lua_close(vm->L); 
}

void vm_bios(VM *vm) {
    if (luaL_dostring(vm->L, BIOS) != LUA_OK) {
        fprintf(stderr, "Failed to boot BIOS apparently: %s\n", lua_tostring(vm->L, -1));
        lua_pop(vm->L, 1);
    }
}
