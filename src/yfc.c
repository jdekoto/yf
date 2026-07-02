// yfc.c
#include "yfc.h"
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

static bool is_valid_yfc_asset(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot) return false; 

    if (strcmp(dot, ".raw") == 0 || 
        strcmp(dot, ".cm")  == 0 || 
        strcmp(dot, ".bin") == 0 || 
        strcmp(dot, ".map") == 0) {
        return true;
    }
    return false;
}

static bool iszeroed(const void *block, size_t size) {
    const unsigned char *p = block;
    for (size_t i = 0; i < size; i++) {
        if (p[i] != 0) return false;
    }
    return true; 
}

char *__strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p) {
        memcpy(p, s, len);
    }
    return p;
}


// Microtar Low-Level IO Hooks
static int file_write(mtar_t *tar, const void *data, unsigned size) {
  unsigned res = fwrite(data, 1, size, tar->stream);
  return (res == size) ? MTAR_ESUCCESS : MTAR_EWRITEFAIL;
}

static int file_read(mtar_t *tar, void *data, unsigned size) {
  unsigned res = fread(data, 1, size, tar->stream);
  return (res == size) ? MTAR_ESUCCESS : MTAR_EREADFAIL;
}

static int file_seek(mtar_t *tar, unsigned offset) {
  int res = fseek(tar->stream, offset, SEEK_SET);
  return (res == 0) ? MTAR_ESUCCESS : MTAR_ESEEKFAIL;
}

static int file_close(mtar_t *tar) {
  fclose(tar->stream);
  return MTAR_ESUCCESS;
}

// --- RECURSIVE CROSS-PLATFORM SYSTEM SCANNER FOR PACKING ---
static int pack_target_filtered(mtar_t *tar, const char *path, bool filter_assets) {
    #ifdef _WIN32
    struct _stat s;
    if (_stat(path, &s) != 0) return -1;
    #define IS_DIR_MODE S_ISDIR(s.st_mode)
    #define IS_REG_MODE S_ISREG(s.st_mode)
    #else
    struct stat s;
    if (stat(path, &s) != 0) return -1;
    #define IS_DIR_MODE S_ISDIR(s.st_mode)
    #define IS_REG_MODE S_ISREG(s.st_mode)
    #endif

    if (IS_REG_MODE) {
        // Apply file extension whitelist inside assets subfolders
        if (filter_assets) {
            const char *filename = strrchr(path, '/');
            if (!filename) filename = strrchr(path, '\\');
            filename = filename ? filename + 1 : path;

            if (!is_valid_yfc_asset(filename)) {
                return 0; // Drop non-whitelisted assets cleanly
            }
        }

        FILE *f = fopen(path, "rb");
        if (!f) return -1;

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        char *buf = malloc(size > 0 ? size : 1);
        if (buf && size > 0) {
            size_t read_bytes = fread(buf, 1, size, f);
            fclose(f);
            
            printf("  [PACK] File: %s (%ld bytes)\n", path, size);
            mtar_write_file_header(tar, path, size);
            mtar_write_data(tar, buf, size);
            free(buf);
        } else {
            fclose(f);
            if (buf) free(buf);
            mtar_write_file_header(tar, path, 0);
        }
        return 0;
    }

    if (IS_DIR_MODE) {
        char dir_name[256];
        snprintf(dir_name, sizeof(dir_name), "%s/", path);
        mtar_write_dir_header(tar, dir_name);

#ifdef _WIN32
        char search_path[512];
        snprintf(search_path, sizeof(search_path), "%s/*", path);

        WIN32_FIND_DATA find_data;
        HANDLE hFind = FindFirstFile(search_path, &find_data);
        if (hFind == INVALID_HANDLE_VALUE) return -1;

        do {
            if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;

            char child_path[512];
            snprintf(child_path, sizeof(child_path), "%s/%s", path, find_data.cFileName);
            pack_target_filtered(tar, child_path, filter_assets);
        } while (FindNextFile(hFind, &find_data));
        FindClose(hFind);
#else
        DIR *d = opendir(path);
        if (!d) return -1;

        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            char child_path[512];
            snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
            pack_target_filtered(tar, child_path, filter_assets);
        }
        closedir(d);
#endif
    }
    return 0;
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
static bool yfc_compile_workspace_to_bytecode(GrowthBuffer *bytecode_destination) {
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

// Parses the clean, space-separated inclusions string extracted by parse_config and packs the files
void pack_inclusions(const char *inclusions_str, mtar_t *tar) {
    // If the inclusions key was empty or completely missing, exit early
    if (!inclusions_str || strlen(inclusions_str) == 0 || strcmp(inclusions_str, "Untitled") == 0) {
        return;
    }

    // Create a local working copy because strtok destructively modifies strings by inserting '\0'
    char *inclusions_copy = __strdup(inclusions_str);
    if (!inclusions_copy) return;

    // Tokenize the string by spaces or tabs to isolate each file path
    char *token = strtok(inclusions_copy, " \t");
    while (token != NULL) {
        if (strlen(token) > 0) {
            // Check if the targeted inclusion file actually exists on the host disk
            #ifdef _WIN32
            struct _stat s;
            int stat_res = _stat(token, &s);
            #else
            struct stat s;
            int stat_res = stat(token, &s);
            #endif

            if (stat_res == 0 && S_ISREG(s.st_mode)) {
                printf("  [PACK] Inclusion Target -> %s\n", token);
                
                // Pack the file directly, passing 'false' to completely bypass the extension whitelist checks
                pack_target_filtered(tar, token, false); 
            } else {
                printf("  [PACK WARNING] Config specified inclusion target not found: '%s'\n", token);
            }
        }
        token = strtok(NULL, " \t");
    }

    free(inclusions_copy); // Clean up duplicate heap string
}

int yfc_pack(const char *cartridge, const char *output) {
    if (chdir(cartridge) != 0) {
        printf("ERROR: Could not open or find cartridge folder: %s\n", cartridge);
        return 1;
    }
  
    char title[32] = "yellowfeather";
    char author[32] = "unknown";
    char version[8] = "1.0.0";
    char inclusions[1024] = " ";

    FILE *cf = fopen("config.txt", "rb");
    if (cf) {
        fseek(cf, 0, SEEK_END);
        long size = ftell(cf);
        fseek(cf, 0, SEEK_SET);
        char *buf = malloc(size + 1);
        if (buf) {
            size_t read_bytes = fread(buf, 1, size, cf);
            buf[size] = '\0';
            parse_config(buf, "title", title, sizeof(title));
            parse_config(buf, "author", author, sizeof(author));
            parse_config(buf, "version", version, sizeof(version));
            parse_config(buf, "inclusions", inclusions, sizeof(inclusions));
        }
        free(buf);
        fclose(cf);
    }

    mtar_t tar;
    if (mtar_open(&tar, output, "w") != MTAR_ESUCCESS) {
        fprintf(stderr, "Error: Could not create output cartridge asset at %s\n", output);
        return -1;
    }

    char pad_title[32] = {0};
    char pad_author[32] = {0};
    char pad_version[8] = {0};

    strncpy(pad_title, title, 32);
    strncpy(pad_author, author, 32);
    strncpy(pad_version, version, 8);

    fwrite("YFC!", 1, 4, (FILE*)tar.stream);
    fwrite(pad_title, 1, 32, (FILE*)tar.stream);
    fwrite(pad_author, 1, 32, (FILE*)tar.stream);
    fwrite(pad_version, 1, 8, (FILE*)tar.stream);
    
    tar.pos = 0;

    printf("[PACKAGER] Starting build for cartridge: %s\n", title);
    
    // pack the lua sources into a single bytecode binary
    GrowthBuffer compiled_bytecode;
    init_growth_buffer(&compiled_bytecode);

    if (!yfc_compile_workspace_to_bytecode(&compiled_bytecode)) {
        fprintf(stderr, "Aborting build package generation due to compilation bugs.\n");
        mtar_close(&tar);
        free_growth_buffer(&compiled_bytecode);
        return -1;
    }

    // Write the secure compiled bytecode out into the tar structure under 'boot.lua'
    printf("  [PACK] Obfuscated Bytecode Binary -> boot.rom (%zu bytes)\n", compiled_bytecode.length);
    mtar_write_file_header(&tar, "boot.rom", compiled_bytecode.length);
    mtar_write_data(&tar, compiled_bytecode.data, compiled_bytecode.length);
    free_growth_buffer(&compiled_bytecode);

    /* TODO: bc of the include macro we just added, this isnt needed anymore. so make folders addable thru inclusions
    #ifdef _WIN32
    struct _stat s;
    if (_stat("assets", &s) == 0 && S_ISDIR(s.st_mode))   pack_target_filtered(&tar, "assets", true);
    #define S_ISDIR_CHECK(p) (_stat(p, &s) == 0 && S_ISDIR(s.st_mode))
    #else
    struct stat s;
    if (stat("assets", &s) == 0 && S_ISDIR(s.st_mode))   pack_target_filtered(&tar, "assets", true);
    #endif
    */
    
    // packs any files defined by the inclusions key in config.txt
    pack_inclusions(inclusions, &tar);

    mtar_finalize(&tar);
    mtar_close(&tar);

    printf("[PACKAGER] Successfully packed cartridge into: %s\n", output);
    return 0;
}

// --- CENTRAL VIRTUAL MACHINE RUNTIME SANDBOX UNPACKER BOOTSTRAP ---
int yfc_boot(VM *vm, const char *cart_path, long offset) {
    char sandbox_path[256];
    #ifdef _WIN32
      const char *win_tmp = getenv("TEMP");
      if (!win_tmp) win_tmp = getenv("TMP");
      if (!win_tmp) win_tmp = "."; 
    
      snprintf(sandbox_path, sizeof(sandbox_path), "%s\\yf_sandbox_%d", win_tmp, getpid());
    #else
      snprintf(sandbox_path, sizeof(sandbox_path), "/tmp/yf_sandbox_%d", getpid());
    #endif

    char rm_cmd[512];
    #ifdef _WIN32
        snprintf(rm_cmd, sizeof(rm_cmd), "rmdir /s /q \"%s\"", sandbox_path);
    #else
        snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", sandbox_path);
    #endif
    system(rm_cmd);

    if (mkdir(sandbox_path, 0700) != 0) {
        perror("Error: Failed to create sandbox folder");
        return -1;
    }

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

    long tar_start_offset = (offset > 0) ? (offset + 76) : 76;
    fseek(fd, tar_start_offset, SEEK_SET);

    mtar_t tar;
    memset(&tar, 0, sizeof(tar));
    
    tar.read = file_read;
    tar.seek = file_seek;
    tar.close = file_close;
    tar.stream = fd; 
    
    tar.pos = tar_start_offset;
    tar.last_header = tar_start_offset;

    if (chdir(sandbox_path) != 0) {
        perror("Error: Failed to navigate into sandbox execution layer");
        mtar_close(&tar);
        return -1;
    }

    mtar_header_t h;
    FILE *fp = (FILE*)tar.stream; 
    
    while (mtar_read_header(&tar, &h) == MTAR_ESUCCESS) {
        size_t name_len = strlen(h.name);
        bool is_dir = (name_len > 0 && h.name[name_len - 1] == '/') || (h.type == MTAR_TDIR);

        if (is_dir) {
            #ifdef _WIN32
                _mkdir(h.name);
            #else
                mkdir(h.name, 0755);
            #endif
            mtar_next(&tar);
        } else {
            char *file_buf = malloc(h.size > 0 ? h.size : 1);
            if (file_buf) {
                fseek(fp, tar.last_header + 512, SEEK_SET);
                size_t read_bytes = fread(file_buf, 1, h.size, fp);
                
                long write_len = h.size;

                // If it's a standard text script, trim training garbage strings at the null terminator.
                // If it begins with the Lua execution signature (\x1bLua), keep every byte intact.
                if (name_len > 4 && strcmp(h.name + name_len - 4, ".lua") == 0) {
                    bool is_binary_bytecode = (h.size >= 4 && 
                                               file_buf[0] == 0x1B && 
                                               file_buf[1] == 'L' && 
                                               file_buf[2] == 'u' && 
                                               file_buf[3] == 'a');

                    if (!is_binary_bytecode) {
                        for (unsigned int i = 0; i < h.size; i++) {
                            if (file_buf[i] == '\0') {
                                write_len = i; 
                                break;
                            }
                        }
                    }
                }

                FILE *wf = fopen(h.name, "wb");
                if (wf) {
                    if (write_len > 0) {
                        fwrite(file_buf, 1, write_len, wf);
                    }
                    fclose(wf);
                }
                free(file_buf);
            }

            unsigned int padded_size = ((h.size + 511) & ~511);
            long next_header_pos = tar.last_header + 512 + padded_size;
            fseek(fp, next_header_pos, SEEK_SET);
            
            tar.pos = next_header_pos;
            tar.last_header = next_header_pos;
        }
    }
    mtar_close(&tar);
    // hell yeah we got compiled binaries booting
    vm_load(vm, "boot.rom");
    return 0;
}
