// yfc.c
#include "headers/yfc.h"
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #include <process.h> 
    #define chdir _chdir
    #define getpid _getpid
    #define mkdir(path, mode) _mkdir(path) 
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <dirent.h> 
#endif

// --- DYNAMIC IN-MEMORY STRING EXPANSION STRUCTURES ---
typedef struct {
    char *data;
    size_t capacity;
    size_t length;
} GrowthBuffer;

static void init_growth_buffer(GrowthBuffer *gb) {
    gb->capacity = 4096;
    gb->length = 0;
    gb->data = malloc(gb->capacity);
    if (gb->data) gb->data[0] = '\0';
}

static void append_growth_buffer(GrowthBuffer *gb, const char *str, size_t len) {
    if (gb->length + len >= gb->capacity) {
        while (gb->length + len >= gb->capacity) {
            gb->capacity *= 2;
        }
        gb->data = realloc(gb->data, gb->capacity);
    }
    memcpy(gb->data + gb->length, str, len);
    gb->length += len;
    gb->data[gb->length] = '\0';
}

static void free_growth_buffer(GrowthBuffer *gb) {
    if (gb->data) {
        free(gb->data);
        gb->data = NULL;
    }
    gb->capacity = 0;
    gb->length = 0;
}

static int yfc_bytecode_encoder_callback(lua_State *L, const void *p, size_t sz, void *ud) {
    GrowthBuffer *bytecode_out = (GrowthBuffer *)ud;
    append_growth_buffer(bytecode_out, (const char *)p, sz);
    return 0;
}

char *__strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p) {
        memcpy(p, s, len);
    }
    return p;
}

// --- NATIVE C LUA SOURCE CODE HARVESTER ---
static void harvest_sources_recursive(const char *root_dir, const char *current_dir, GrowthBuffer *gb) {
#ifdef _WIN32
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s/*", current_dir);

    WIN32_FIND_DATA find_data;
    HANDLE hFind = FindFirstFile(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", current_dir, find_data.cFileName);

        struct _stat s;
        if (_stat(full_path, &s) == 0) {
            if (S_ISDIR(s.st_mode)) {
                harvest_sources_recursive(root_dir, full_path, gb);
            } 
            else if (S_ISREG(s.st_mode) && strstr(find_data.cFileName, ".lua") != NULL) {
#else
    DIR *d = opendir(current_dir);
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", current_dir, entry->d_name);

        struct stat s;
        if (stat(full_path, &s) == 0) {
            if (S_ISDIR(s.st_mode)) {
                harvest_sources_recursive(root_dir, full_path, gb);
            } 
            else if (S_ISREG(s.st_mode) && strstr(entry->d_name, ".lua") != NULL) {
                const char* filename = entry->d_name;
#endif
                // Calculate and format the package module dictionary key name
                char module_key[512] = {0};
                const char *relative_part = full_path + strlen(root_dir);
                if (relative_part[0] == '/' || relative_part[0] == '\\') relative_part++;
                
                strncpy(module_key, relative_part, sizeof(module_key) - 1);
                char *dot = strrchr(module_key, '.');
                if (dot) *dot = '\0';

                // Standardize path separators to dot notation keys
                for (int i = 0; module_key[i] != '\0'; i++) {
                    if (module_key[i] == '/' || module_key[i] == '\\') module_key[i] = '.';
                }

                FILE *sf = fopen(full_path, "rb");
                if (sf) {
                    fseek(sf, 0, SEEK_END);
                    long sf_size = ftell(sf);
                    fseek(sf, 0, SEEK_SET);

                    char *sf_buf = malloc(sf_size + 1);
                    size_t read_bytes = fread(sf_buf, 1, sf_size, sf);
                    sf_buf[sf_size] = '\0';
                    fclose(sf);

                    char wrapper_header[512];
                    int header_len = snprintf(wrapper_header, sizeof(wrapper_header), 
                                              "package.preload[\"%s\"] = function(...)\n", module_key);  
                    append_growth_buffer(gb, wrapper_header, header_len);
                    append_growth_buffer(gb, sf_buf, sf_size);
                    append_growth_buffer(gb, "\nend\n\n", 6);

                    free(sf_buf);
                }
            }
        }
#ifdef _WIN32
    } while (FindNextFile(hFind, &find_data));
    FindClose(hFind);
#else
    }
    closedir(d);
#endif
}

// --- CONSOLIDATED NATIVE PRELOAD BYTECODE COMPILER COMPONENT ---
static bool yfc_compile(GrowthBuffer *bytecode_destination) {
    GrowthBuffer master_source_text;
    init_growth_buffer(&master_source_text);

    // 1. Process and pack internal structural modules inside the sources folder
    #ifdef _WIN32
    struct _stat s;
    if (_stat("sources", &s) == 0 && S_ISDIR(s.st_mode)) {
    #else
    struct stat s;
    if (stat("sources", &s) == 0 && S_ISDIR(s.st_mode)) {
    #endif
        harvest_sources_recursive("sources", "sources", &master_source_text);
    }

    // 2. Wrap and secure the main execution frame setup module (boot.lua)
    FILE *bf = fopen("boot.lua", "rb");
    if (!bf) {
        printf("Core Pack Error: Missing mandatory anchor module: boot.lua\n");
        free_growth_buffer(&master_source_text);
        return false;
    }
    fseek(bf, 0, SEEK_END);
    long bf_size = ftell(bf);
    fseek(bf, 0, SEEK_SET);

    char *bf_buf = malloc(bf_size + 1);
    size_t read_bytes = fread(bf_buf, 1, bf_size, bf);
    bf_buf[bf_size] = '\0';
    fclose(bf);

    append_growth_buffer(&master_source_text, "\n-- Main System Bootstrap Hook\n", 31);
    append_growth_buffer(&master_source_text, bf_buf, bf_size);
    free(bf_buf);

    // 3. Sprout isolated Lua instance to process the unified source buffer
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    int load_status = luaL_loadbuffer(L, master_source_text.data, master_source_text.length, "=boot.lua");
    free_growth_buffer(&master_source_text); // Raw strings are no longer needed

    if (load_status != LUA_OK) {
        printf("❌ Game Packaging Compilation Error:\n%s\n", lua_tostring(L, -1));
        lua_close(L);
        return false;
    }

    // Serialize out to binary bytecode, stripping symbol/debug line information (strip flag = 1)
    lua_dump(L, yfc_bytecode_encoder_callback, bytecode_destination, 1);
    
    lua_close(L);
    return true;
}

// --- CLEANED SEQUENTIAL STREAM PACKER ---
int yfc_pack(const char *cartridge, const char *output) {
    if (chdir(cartridge) != 0) {
        printf("ERROR: Could not open or find cartridge folder: %s\n", cartridge);
        return 1;
    }
    
    char id[8] = "BLANK";
    char title[32] = "yellowfeather";
    char author[32] = "unknown";
    char version[8] = "1.0.0";

    FILE *cf = fopen("config.txt", "rb");
    if (cf) {
        fseek(cf, 0, SEEK_END);
        long size = ftell(cf);
        fseek(cf, 0, SEEK_SET);
        char *buf = malloc(size + 1);
        if (buf) {
            size_t read_bytes = fread(buf, 1, size, cf);
            buf[size] = '\0';
            parse_config(buf, "id", id, sizeof(id));
            parse_config(buf, "title", title, sizeof(title));
            parse_config(buf, "author", author, sizeof(author));
            parse_config(buf, "version", version, sizeof(version));
        }
        free(buf);
        fclose(cf);
    }

    FILE *f_out = fopen(output, "wb");
    if (!f_out) {
        fprintf(stderr, "Error: Could not create output cartridge asset at %s\n", output);
        return -1;
    }
    
    char pad_id[8] = {0};
    char pad_title[32] = {0};
    char pad_author[32] = {0};
    char pad_version[8] = {0};

    strncpy(pad_id, id, 8);
    strncpy(pad_title, title, 32);
    strncpy(pad_author, author, 32);
    strncpy(pad_version, version, 8);

    // Write structural binary header directly via standard io
    fwrite("YFC!", 1, 4, f_out);
    fwrite(pad_id, 1, 8, f_out);
    fwrite(pad_title, 1, 32, f_out);
    fwrite(pad_author, 1, 32, f_out);
    fwrite(pad_version, 1, 8, f_out);
    
    printf("[PACKAGER] Starting binary build for cartridge: %s (ID: %s)\n", title, id);
    
    GrowthBuffer compiled_bytecode;
    init_growth_buffer(&compiled_bytecode);

    if (!yfc_compile(&compiled_bytecode)) {
        fprintf(stderr, "Aborting build package generation due to compilation bugs.\n");
        fclose(f_out);
        free_growth_buffer(&compiled_bytecode);
        return -1;
    }

    printf("  [PACK] Obfuscated Bytecode Binary -> Appending raw data (%zu bytes)\n", compiled_bytecode.length);
    
    // 1. Write the explicit size of the bytecode segment (4-byte descriptor)
    uint32_t bytecode_len = (uint32_t)compiled_bytecode.length;
    fwrite(&bytecode_len, sizeof(uint32_t), 1, f_out);
    
    // 2. Stream the raw compiled binary bytes directly behind it
    fwrite(compiled_bytecode.data, 1, bytecode_len, f_out);

    free_growth_buffer(&compiled_bytecode);
    fclose(f_out);

    printf("[PACKAGER] Successfully packed cartridge into: %s\n", output);
    return 0;
}

// --- STREAMLINED LIGHTWEIGHT BOOTSTRAP UNPACKER ---
int yfc_boot(VM *vm, const char *cart_path, long offset) {
    FILE *fd = fopen(cart_path, "rb");
    if (!fd) {
        fprintf(stderr, "Error: Could not open file at %s\n", cart_path);
        return -1;
    }

    if (offset > 0) {
        fseek(fd, offset, SEEK_SET);
    } else {
        fseek(fd, 0, SEEK_SET);
    }

    char magic[4];
    if (fread(magic, 1, 4, fd) != 4 || strncmp(magic, "YFC!", 4) != 0) {
        fprintf(stderr, "Error: Invalid cartridge format or missing signature.\n");
        fclose(fd);
        return -1;
    }

    char cart_id[8] = {0};
    char cart_title[32] = {0};
    char cart_author[32] = {0};
    char cart_version[8] = {0};

    fread(cart_id, 1, 8, fd);
    fread(cart_title, 1, 32, fd);
    fread(cart_author, 1, 32, fd);
    fread(cart_version, 1, 8, fd);

    // Read the size descriptor for the appended bytecode segment
    uint32_t bytecode_len = 0;
    if (fread(&bytecode_len, sizeof(uint32_t), 1, fd) != 1) {
        fprintf(stderr, "Error: Failed to read bytecode size from cartridge.\n");
        fclose(fd);
        return -1;
    }

    // Allocate memory and ingest the raw binary payload directly from disk
    char *bytecode_buf = malloc(bytecode_len);
    if (!bytecode_buf) {
        fprintf(stderr, "Error: Out of memory allocated for bytecode buffer.\n");
        fclose(fd);
        return -1;
    }

    if (fread(bytecode_buf, 1, bytecode_len, fd) != bytecode_len) {
        fprintf(stderr, "Error: Incomplete bytecode data read.\n");
        free(bytecode_buf);
        fclose(fd);
        return -1;
    }
    fclose(fd);
    
    strncpy(vm->id, cart_id, 8);

    printf("[BOOT] Running '%s' by %s (Version: %s) [ID: %s]\n", cart_title, cart_author, cart_version, cart_id);
  
    vm_execute(vm, (const char*)bytecode_buf, bytecode_len, cart_id);
    return 0;
}
