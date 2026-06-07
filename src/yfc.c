#include "yfc.h"
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>

#ifdef _WIN32
    #include <direct.h>
    #define chdir _chdir
#else
    #include <unistd.h>
#endif

static unsigned int oct2uint(const char *oct, unsigned int size) {
    unsigned int out = 0;
    unsigned int i = 0;

    // Skip any leading spaces or padding characters often found in TAR headers
    while (i < size && (oct[i] == ' ' || oct[i] == '\0')) {
        i++;
    }

    // Process valid octal character digits ('0' through '7')
    while (i < size && oct[i] >= '0' && oct[i] <= '7') {
        // Shifting left by 3 bits is exactly equivalent to multiplying by 8
        out = (out << 3) | (unsigned int)(oct[i] - '0');
        i++;
    }

    return out;
}

static bool iszeroed(const void *block, size_t size) {
    const unsigned char *p = block;
    for (size_t i = 0; i < size; i++) {
        if (p[i] != 0) {
            return false; // Found a non-zero byte
        }
    }
    return true; // All bytes are zero
}

int yfc_pack(const char *cartridge, const char *output) {
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

    int out_fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        perror("Error: Could not create output file");
        return -1;
    }

    char pad_title[32] = {0};
    char pad_author[32] = {0};
    char pad_version[8] = {0};

    strncpy(pad_title, title, 32);
    strncpy(pad_author, author, 32);
    strncpy(pad_version, version, 8);

    write(out_fd, "YFC!", 4);
    write(out_fd, pad_title, 32);
    write(out_fd, pad_author, 32);
    write(out_fd, pad_version, 8);

    const char *targets[4];
    size_t count = 0;
    struct stat s;

    if (stat("boot.lua", &s) == 0) {
        targets[count++] = "boot.lua";
    } else {
        fprintf(stderr, "Critical Error: 'boot.lua' not found in project directory!\n");
        close(out_fd);
        return -1;
    }

    if (stat("sources", &s) == 0 && S_ISDIR(s.st_mode)) {
        targets[count++] = "sources";
    }
    if (stat("assets", &s) == 0 && S_ISDIR(s.st_mode)) {
        targets[count++] = "assets";
    }
    if (stat("external", &s) == 0 && S_ISDIR(s.st_mode)) {
        targets[count++] = "external";
    }

    struct tar_t *archive = NULL;
    struct tar_t *head = NULL;
    int offset = 76;

    if (write_entries(out_fd, &archive, &head, count, targets, &offset, 0) < 0) {
        fprintf(stderr, "Failed archiving local files.\n");
        close(out_fd);
        return -1;
    }
    write_end_data(out_fd, offset, 0);

    tar_free(archive);
    close(out_fd);
    printf("Successfully packed cartridge into: %s\n", output);
    return 0;
}

int yfc_verify(const char *cart_path) {
    int fd = open(cart_path, O_RDONLY);
    lseek(fd, 76, SEEK_SET);   /* skip yfc header */

    uint8_t block[512];
    int entry = 0;
    while (read(fd, block, 512) == 512) {
        if (block[0] == '\0') break;   /* end of archive */
        /* size field is at offset 124, 12 bytes octal */
        char size_buf[13] = {0};
        memcpy(size_buf, block + 124, 12);
        unsigned int size = (unsigned int)strtol(size_buf, NULL, 8);
        printf("entry %d: name=%.100s size=%u\n", entry++, block, size);
        /* skip data blocks */
        lseek(fd, ((size + 511) / 512) * 512, SEEK_CUR);
    }
    close(fd);
    return 0;
}

int yfc_boot(VM *vm, const char *cart_path, long offset) {
    // 1. Open the file descriptor first before messing with current working directories
    int fd = open(cart_path, O_RDONLY);
    if (fd < 0) {
        perror("Error: Could not open cartridge file");
        close(fd);
        return -1;
    }
    
    if (offset > 0) {
        lseek(fd, offset, SEEK_SET);
    } else {
        lseek(fd, 0, SEEK_SET); // Ensure we start at the true beginning of a standalone file
    }

    // Verify 'YFC!' magic identifier token
    char magic[4];
    if (read(fd, magic, 4) != 4 || strncmp(magic, "YFC!", 4) != 0) {
        fprintf(stderr, "Error: Invalid cartridge format.\n");
        close(fd);
        return -1;
    }

    // Move file descriptor past the 76-byte custom header matrix straight to the TAR payload
    // Establish exactly where the TAR archive headers begin
    off_t current_offset;
    if (offset > 0) {
        current_offset = offset + 76; // Fused execution mode path offset
    } else {
        current_offset = 76;          // Standalone cartridge execution path offset
    }

    // 2. Map a safe sandbox path inside your Linux /tmp RAM disk environment
    char sandbox_path[256];
    snprintf(sandbox_path, sizeof(sandbox_path), "/tmp/yf_sandbox_%d", getpid());

    // Clean up any stale configurations from a previously crashed session
    char rm_cmd[512];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", sandbox_path);
    system(rm_cmd);

    if (mkdir(sandbox_path, 0700) != 0) { // Secure user permissions
        perror("Error: Failed to create sandbox folder");
        close(fd);
        return -1;
    }

    // 3. Move engine's execution path into the clean workspace sandbox
    if (chdir(sandbox_path) != 0) {
        perror("Error: Failed to navigate into sandbox execution layer");
        close(fd);
        return -1;
    }
    
    // seek it at the correct offset since the magic checker throws it off a lil bit.
    lseek(fd, current_offset, SEEK_SET);

    // 4. Let your native library map out the file structural node array
    struct tar_t *archive = NULL;
    if (tar_read(fd, &archive, 0) < 0) {
        fprintf(stderr, "Error: Failed to parse internal archive stream layout.\n");
        close(fd);
        return -1;
    }
    
    struct tar_t *cur = archive;

    while (cur) {
        unsigned int file_size = oct2uint(cur->size, 12);
        unsigned int padded_size = file_size;
        if (file_size % 512) {
            padded_size += 512 - (file_size % 512);
        }

        // Check if we hit the trailing empty block markers of the TAR archive
        lseek(fd, current_offset, SEEK_SET);
        char block_check[512];
        if (read(fd, block_check, 512) != 512 || iszeroed(block_check, 512)) {
            break; 
        }

        // Determine if this entry is a directory or a regular file.
        // In TAR, directories end with a '/' or have a typeflag/size of 0.
        size_t name_len = strlen(cur->name);
        bool is_dir = (name_len > 0 && cur->name[name_len - 1] == '/') || (file_size == 0);

        if (is_dir) {
            // It's a directory! Create it standardly in our sandbox
            #ifdef _WIN32
                _mkdir(cur->name);
            #else
                mkdir(cur->name, 0755);
            #endif
        } else {
            // It's a regular file! Let's jump right past its 512-byte header block
            lseek(fd, current_offset + 512, SEEK_SET);

            char *file_buf = malloc(file_size);
            if (file_buf) {
                if (read(fd, file_buf, file_size) == (ssize_t)file_size) {
                    
                    long write_len = file_size;

                    // --- TARGETED LUA TEXT TRUNCATION FIX ---
                    // If it's a Lua file, strip trailing packer padding bytes directly in memory
                    if (name_len > 4 && strcmp(cur->name + name_len - 4, ".lua") == 0) {
                        for (unsigned int i = 0; i < file_size; i++) {
                            if (file_buf[i] == '\0') {
                                write_len = i; // Cut off right where the padding begins
                                break;
                            }
                        }
                    }

                    // Write the clean, unpadded data out to our sandbox folder
                    FILE *wf = fopen(cur->name, "wb");
                    if (wf) {
                        if (write_len > 0) {
                            fwrite(file_buf, 1, write_len, wf);
                        }
                        fclose(wf);
                    } else {
                        fprintf(stderr, "Error: Could not write file %s\n", cur->name);
                    }
                }
                free(file_buf);
            }
        }

        // Advance to the next entry's header position block exactly
        current_offset += 512 + padded_size;
        cur = cur->next;
    }
    
    // Free tracking linked list links
    tar_free(archive);
    close(fd);
    vm_load(vm, "boot.lua");
    return 0;
}
