#include "vm.h"
#include "mem.h"
#include "audio.h"
#include "runtime.h"
#include <string.h>
#include <dirent.h>


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
    if (f) {
        // Find out how big the file is
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        // Safety check to ensure we don't overflow the system RAM limits
        if (dest_addr + size <= RAM_SIZE) {
            fread(memory + dest_addr, 1, size, f);
        }
        fclose(f);
    }
    return 0;
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

void vm_init(VM *vm) {
    vm->L = luaL_newstate();
    luaL_openlibs(vm->L);

    /* expose API globally */
    lua_getglobal(vm->L, "_G");
    luaL_setfuncs(vm->L, api, 0);
    lua_pop(vm->L, 1);
    
    vm_runtime(vm);
}

void vm_load(VM *vm, const char *path) {
    
  
    if (luaL_dofile(vm->L, path) != LUA_OK) {
        fprintf(stderr, "vm_load: %s\n", lua_tostring(vm->L, -1));
        lua_pop(vm->L, 1);
    }
    
    call_fn(vm->L, "_boot");
}

void vm_update  (VM *vm) { 
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
