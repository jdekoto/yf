// yfc.h
#ifndef YFC_H
#define YFC_H

#include <stdint.h>
#include "vm.h"

#define YFC_MAGIC "YFC!"
#define MAX_CART_SIZE (16 * 1024 * 1024) // Strict 16MB threshold

typedef struct __attribute__((packed)) {
    char magic[4];          // "YFC!"
    char title[32];         // From config.txt
    char author[32];        // From config.txt
    char version[8];        // From config.txt
} YfcArchiveHeader;

// Standard POSIX TAR layout block used for our file tree tracking
typedef struct __attribute__((packed)) {
    char name[100];     // File path name (e.g. "assets/music.xm")
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];      // Size stored as an Octal text string!
    char mtime[12];
    char chksum[8];
    char typeflag;      // '0' or '\0' for normal file, '5' for directory
    char linkname[100];
    char magic[6];      // "ustar"
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];   // Pads the structure perfectly to 512 bytes
} TarHeader;

int boot_yfc(VM *vm, const char* file);

#endif
