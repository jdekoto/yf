#include "yfc.h"
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

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
    #include <dirent.h> // Standard directory streams
#endif

static bool iszeroed(const void *block, size_t size) {
    const unsigned char *p = block;
    for (size_t i = 0; i < size; i++) {
        if (p[i] != 0) {
            return false; // Found a non-zero byte
        }
    }
    return true; // All bytes are zero
}

// pulled STRAIGHT out of microtar
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

static void fix_path_slashes(char *path) {
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '\\') {
            path[i] = '/';
        }
    }
}
// Recursive helper to pack files and directory trees inside your targets
static int pack_target(mtar_t *tar, const char *path) {
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

    // Case 1: If it's a regular file, read it and pack it
    if (IS_REG_MODE) {
        FILE *f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "  [PACK] Warning: Could not open file %s\n", path);
            return -1;
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        char *buf = malloc(size > 0 ? size : 1);
        if (buf && size > 0) {
            size_t read_bytes = fread(buf, 1, size, f);
            fclose(f);

            // Log the file inclusion
            printf("  [PACK] File: %s (%ld bytes)\n", path, size);
            
            // Write standard file header and raw contents out via microtar
            mtar_write_file_header(tar, path, size);
            mtar_write_data(tar, buf, size);
            free(buf);
        } else {
            fclose(f);
            if (buf) free(buf);
            // Write empty files cleanly if any exist
            mtar_write_file_header(tar, path, 0);
        }
        return 0;
    }

    // Case 2: If it's a directory, write a directory header and crawl its subfolders recursively
    if (IS_DIR_MODE) {
        printf("  [PACK] Directory: %s/\n", path);

        // Microtar directory entries must end with a trailing slash
        char dir_name[256];
        snprintf(dir_name, sizeof(dir_name), "%s/", path);
        mtar_write_dir_header(tar, dir_name);

#ifdef _WIN32
        // --- WINDOWS NATIVE DIRECTORY CRAWLER ---
        char search_path[512];
        // Windows needs a wildcard (* or *.*) to search inside a folder
        snprintf(search_path, sizeof(search_path), "%s/*", path);

        WIN32_FIND_DATA find_data;
        HANDLE hFind = FindFirstFile(search_path, &find_data);

        if (hFind == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "  [PACK] Error: Could not read directory %s\n", path);
            return -1;
        }

        do {
            // Absolutely skip relative dot directory links to avoid infinite loops
            if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
                continue;
            }

            char child_path[512];
            snprintf(child_path, sizeof(child_path), "%s/%s", path, find_data.cFileName);
            
            // Recursively pack nested items
            pack_target(tar, child_path);

        } while (FindNextFile(hFind, &find_data));

        FindClose(hFind); // Clean up the search handle
#else
        // --- POSIX DIRECTORY CRAWLER (Linux / macOS) ---
        DIR *d = opendir(path);
        if (!d) {
            fprintf(stderr, "  [PACK] Error: Could not read directory %s\n", path);
            return -1;
        }

        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char child_path[512];
            snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
            
            pack_target(tar, child_path);
        }
        closedir(d);
#endif
    }
    return 0;
}

int yfc_pack(const char *cartridge, const char *output) {
    // 1. Enter the target folder workspace to gather user game assets
    if (chdir(cartridge) != 0) {
        printf("ERROR: Could not open or find cartridge folder: %s\n", cartridge);
        return 1;
    }
  
    char title[32] = "yf_game";
    char author[32] = "unknown";
    char version[8] = "1.0.0";

    FILE *cf = fopen("config.txt", "rb");
    if (cf) {
        fseek(cf, 0, SEEK_END);
        long size = ftell(cf);
        fseek(cf, 0, SEEK_SET);
        char *buf = malloc(size + 1);
        if (buf) {
            fread(buf, 1, size, cf);
            buf[size] = '\0';
            parse_config(buf, "title", title, sizeof(title));
            parse_config(buf, "author", author, sizeof(author));
            parse_config(buf, "version", version, sizeof(version));
        }
        free(buf);
        fclose(cf);
    }

    // 2. Open up your microtar object writer directly targeting your output destination
    mtar_t tar;
    if (mtar_open(&tar, output, "w") != MTAR_ESUCCESS) {
        fprintf(stderr, "Error: Could not create output cartridge asset at %s\n", output);
        return -1;
    }

    // 3. Construct your custom 76-byte Cartridge Identification Header Segment
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
    
    // Explicitly set microtar's internal cursor register past our custom 76-byte header
    tar.pos = 0;

    // 4. Queue up your engine's target structural subdirectories
    printf("[PACKAGER] Starting build for cartridge: %s\n", title);
    
    // Pack boot.lua first as it's the core entry point
    pack_target(&tar, "boot.lua");

    // Explicitly process the known target directories if they exist
    #ifdef _WIN32
    struct _stat s;
    if (_stat("sources", &s) == 0 && S_ISDIR(s.st_mode))  pack_target(&tar, "sources");
    if (_stat("assets", &s) == 0 && S_ISDIR(s.st_mode))   pack_target(&tar, "assets");
    if (_stat("external", &s) == 0 && S_ISDIR(s.st_mode)) pack_target(&tar, "external");
    #else
    struct stat s;
    if (stat("sources", &s) == 0 && S_ISDIR(s.st_mode))  pack_target(&tar, "sources");
    if (stat("assets", &s) == 0 && S_ISDIR(s.st_mode))   pack_target(&tar, "assets");
    if (stat("external", &s) == 0 && S_ISDIR(s.st_mode)) pack_target(&tar, "external");
    #endif

    // 5. Write final trailing markers and close the stream safely
    mtar_finalize(&tar);
    mtar_close(&tar);

    printf("[PACKAGER] Successfully packed cartridge into: %s\n", output);
    return 0;
}

int yfc_boot(VM *vm, const char *cart_path, long offset) {
    // 1. Setup sandbox directories standardly
    char sandbox_path[256];
    #ifdef _WIN32
    // Grab the standard user temp directory (e.g., C:\Users\Name\AppData\Local\Temp)
      const char *win_tmp = getenv("TEMP");
      if (!win_tmp) win_tmp = getenv("TMP");
      if (!win_tmp) win_tmp = "."; // Hard fallback to local folder if env vars are missing
    
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

    // --- THE OFFSET-FIRST FIX: MANUALLY CONTROL THE FILE OPENING ---
    FILE *fd = fopen(cart_path, "rb");
    if (!fd) {
        fprintf(stderr, "Error: Could not open file at %s\n", cart_path);
        return -1;
    }

    // Apply your dynamic offset directly to the raw file pointer first!
    if (offset > 0) {
        fseek(fd, offset, SEEK_SET);
    } else {
        fseek(fd, 0, SEEK_SET);
    }

    // Verify your signature token directly from the custom header spot
    char magic[4];
    if (fread(magic, 1, 4, fd) != 4 || strncmp(magic, "YFC!", 4) != 0) {
        fprintf(stderr, "Error: Invalid cartridge format or missing signature.\n");
        fclose(fd);
        return -1;
    }

    // Advance past the remaining 72 bytes of custom cart description header space
    long tar_start_offset = (offset > 0) ? (offset + 76) : 76;
    fseek(fd, tar_start_offset, SEEK_SET);

    // --- MANUALLY INITIALIZE MICROTAR STRUCT FROM CHOSEN POSITION ---
    mtar_t tar;
    memset(&tar, 0, sizeof(tar));
    
    // Direct binding setup matching microtar.c's native behavior
    tar.read = file_read;
    tar.seek = file_seek;
    tar.close = file_close;
    tar.stream = fd; // Hand over your pre-positioned file stream pointer!
    
    // Sync microtar's bookkeeping coordinates perfectly with the raw stream cursor
    tar.pos = tar_start_offset;
    tar.last_header = tar_start_offset;

    // 3. Move engine path safely inside the sandbox execution space
    if (chdir(sandbox_path) != 0) {
        perror("Error: Failed to navigate into sandbox execution layer");
        mtar_close(&tar);
        return -1;
    }

    mtar_header_t h;
    FILE *fp = (FILE*)tar.stream; // Access our raw pre-offset file handle
    
    // 4. Iterate over records dynamically and linearly
    while (mtar_read_header(&tar, &h) == MTAR_ESUCCESS) {
        
        size_t name_len = strlen(h.name);
        bool is_dir = (name_len > 0 && h.name[name_len - 1] == '/') || (h.type == MTAR_TDIR);

        if (is_dir) {
            #ifdef _WIN32
                _mkdir(h.name);
            #else
                mkdir(h.name, 0755);
            #endif
            
            // For directories, call mtar_next to step past the directory record block safely
            mtar_next(&tar);
        } else {
            // Allocate space for the file payload
            char *file_buf = malloc(h.size > 0 ? h.size : 1);
            if (file_buf) {
                // --- THE 512-BYTE HEADER SKIP FIX ---
                // mtar_read_header rewinds the file pointer to the START of the header block!
                // We MUST explicitly seek past those 512 bytes to read the true file payload!
                fseek(fp, tar.last_header + 512, SEEK_SET);
                
                size_t read_bytes = fread(file_buf, 1, h.size, fp);
                
                long write_len = h.size;

                // --- TARGETED LUA TEXT TRUNCATION ---
                if (name_len > 4 && strcmp(h.name + name_len - 4, ".lua") == 0) {
                    for (unsigned int i = 0; i < h.size; i++) {
                        if (file_buf[i] == '\0') {
                            write_len = i; 
                            break;
                        }
                    }
                }

                // Write the asset file out to the sandbox folder
                FILE *wf = fopen(h.name, "wb");
                if (wf) {
                    if (write_len > 0) {
                        fwrite(file_buf, 1, write_len, wf);
                    }
                    fclose(wf);
                    printf("  [BOOT] Extracted: %s (%ld bytes)\n", h.name, write_len);
                }
                free(file_buf);
            }

            // --- MANUALLY ALIGN STREAM TO NEXT 512-BYTE MULTIPLE BLOCK ---
            unsigned int padded_size = ((h.size + 511) & ~511);
            
            // Advance the raw file pointer safely past the header, the payload, and the padding
            long next_header_pos = tar.last_header + 512 + padded_size;
            fseek(fp, next_header_pos, SEEK_SET);
            
            // Update microtar's internal markers so it knows exactly where it is standing!
            tar.pos = next_header_pos;
            tar.last_header = next_header_pos;
        }
    }
    mtar_close(&tar);

    printf("[MICROTAR] Pre-offset sandbox initialized perfectly. Booting...\n");
    vm_load(vm, "boot.lua");
    return 0;
}
