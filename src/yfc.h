// yfc.h
#ifndef YFC_H
#define YFC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include "microtar.h"
#include "config.h"
#include "vm.h"

#define MAX_CART_SIZE (16 * 1024 * 1024) // 16MB limit

int yfc_verify(const char *cart_path);
int yfc_pack(const char *cartridge, const char *output);
int yfc_boot(VM *vm, const char *cart_path, long offset);

#endif
