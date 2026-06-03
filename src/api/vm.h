#ifndef VM_H
#define VM_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <stdbool.h>


typedef struct { lua_State *L; } VM;

void vm_init    (VM *vm);
void vm_runtime (VM *vm, const char *folder);
void vm_load    (VM *vm, const char *path);
void vm_update  (VM *vm);
void vm_shutdown(VM *vm);
void vm_reload  (VM *vm, const char *path);

#endif
