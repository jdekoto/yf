/*
 * api.h — Yellow Feather C API header
 */

#ifndef API_H
#define API_H

#include <lua.h>

/* call once in vm_init() after luaL_openlibs()
   registers all graphics, input, and tilemap functions into _G
   along with ADDR_* and BTN_* constants. */
void api_register(lua_State *L);

#endif
