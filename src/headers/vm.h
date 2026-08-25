#ifndef VM_H
#define VM_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <stdbool.h>
#include "api.h"


typedef struct { lua_State *L; char id[8]; } VM;

void vm_init    (VM *vm);
void vm_runtime (VM *vm);
void vm_load    (VM *vm, const char *path);
void vm_execute (VM *vm, const char* buf, int len, const char* name);
void vm_update  (VM *vm);
void vm_shutdown(VM *vm);
void vm_reload  (VM *vm, const char *path);
void vm_bios    (VM *vm);

#endif
