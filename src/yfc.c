// yfc.c
// this shi broke as hell
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "yfc.h"
#include "mem.h"

static FILE* g_cart_fd = NULL;
static uint32_t g_archive_data_start = 0;

// Helper: Converts a standard tar octal-text size string into a clean C integer
static uint32_t parse_octal(const char* p, int max_chars) {
    uint32_t n = 0;
    while (*p >= '0' && *p <= '7' && max_chars-- > 0) {
        n = (n << 3) + (*p - '0');
        p++;
    }
    return n;
}

static bool init_yfc_cartridge_vfs(const char* filepath) {
    g_cart_fd = fopen(filepath, "rb");
    if (!g_cart_fd) {
        printf("ENGINE ERROR: Failed to load cartridge binary: %s\n", filepath);
        return false;
    }

    YfcArchiveHeader header;
    if (fread(&header, sizeof(YfcArchiveHeader), 1, g_cart_fd) != 1) {
        fclose(g_cart_fd);
        return false;
    }

    if (strncmp(header.magic, YFC_MAGIC, 4) != 0) {
        printf("ENGINE ERROR: Invalid file type signature matches.\n");
        fclose(g_cart_fd);
        return false;
    }

    g_archive_data_start = sizeof(YfcArchiveHeader);
    printf("--- Mounting System Bundle: %s v%s by %s ---\n", header.title, header.version, header.author);
    return true;
}

// Scans the internal file system space for a targeted relative directory path
static uint8_t* archive_read_file(const char* target_path, uint32_t* out_size) {
    if (!g_cart_fd) return NULL;

    // Reset read cursor directly to the beginning of our archived storage deck
    fseek(g_cart_fd, g_archive_data_start, SEEK_SET);
    TarHeader th;

    while (fread(&th, sizeof(TarHeader), 1, g_cart_fd) == 1) {
        // Tar files append large blocks of zeros at the end of the payload stream
        if (th.name[0] == '\0') break; 

        uint32_t file_size = parse_octal(th.size, 12);
        
        // Match the targeted path directly
        if (strcmp(th.name, target_path) == 0 && th.typeflag != '5') {
            uint8_t* out_buffer = malloc(file_size + 1);
            fread(out_buffer, 1, file_size, g_cart_fd);
            out_buffer[file_size] = '\0'; // Clean termination block for scripts

            if (out_size) *out_size = file_size;
            return out_buffer;
        }

        // Jump past the current file data contents to check the next sequential header
        // Tar file data layouts always align directly to 512-byte chunk increments
        uint32_t padded_data_size = ((file_size + 511) / 512) * 512;
        fseek(g_cart_fd, padded_data_size, SEEK_CUR);
    }

    return NULL; // Target file path string not located inside our package mapping
}

int boot_yfc(VM *vm, const char* file) {
        // Inside your main engine initialization code block:
        if (!init_yfc_cartridge_vfs(file)) {
            return -1;
        }

        uint32_t boot_code_size = 0;
        // Read the entry script directly from the archive root
        uint8_t* boot_script = archive_read_file("boot.lua", &boot_code_size);

        if (!boot_script) {
            printf("CRITICAL ENGINE ERROR: Core file 'boot.lua' not found in cartridge root!\n");
            return -1;
        }

        // Make sure it doesn't overflow your 512KB hardware ceiling boundaries
        if (ADDR_CART + boot_code_size >= 0x80000) {
            printf("CRITICAL ERROR: Boot script size overflows system constraints.\n");
            free(boot_script);
            return -1;
        }

        memcpy(&memory[ADDR_CART], boot_script, boot_code_size);
        memory[ADDR_CART + boot_code_size] = '\0';
        free(boot_script);
        
        
        // Execute your script space directly
        vm_load(vm, (const char*)memory[ADDR_CART]);
        return 0;
}
