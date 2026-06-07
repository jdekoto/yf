#include "config.h"

// this is both for the window title and the cart header

// Helper function to extract a string value by its key
void parse_config(const char *file_content, const char *key, char *output, int max_len) {
    const char *line = file_content;
    
    while (line && *line) {
        // Check if the current line starts with our key word
        if (strncmp(line, key, strlen(key)) == 0) {
            const char *value_ptr = line + strlen(key);
            
            // Skip past any extra spaces, tabs, or '=' characters to find the raw text value
            while (*value_ptr && (isspace((unsigned char)*value_ptr) || *value_ptr == '=')) {
                value_ptr++;
            }
            
            // Copy characters into our destination buffer until a newline or size limit hits
            int i = 0;
            while (*value_ptr && *value_ptr != '\n' && *value_ptr != '\r' && i < (max_len - 1)) {
                output[i++] = *value_ptr++;
            }
            output[i] = '\0'; // Always null-terminate your string!
            return;
        }
        
        // Move to the next line by looking for the next newline character
        line = strchr(line, '\n');
        if (line) line++;
    }
    
    // Fallback if the key was completely missing from the file
    strncpy(output, "Untitled", max_len);
}

void config_title(SDL_Window *window, const char *config_file_path) {
    FILE *f = fopen(config_file_path, "rb");
    if (!f) {
        // If the file doesn't exist yet, just set a standard default title
        SDL_SetWindowTitle(window, "yf");
        return;
    }
    
    // 1. Calculate file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // 2. Allocate space and read the whole file into a C string buffer
    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return;
    }
    fread(buffer, 1, size, f);
    buffer[size] = '\0'; // Force string to terminate cleanly
    fclose(f);
    
    // 3. Create buffers to hold our parsed outputs
    char game_title[64] = {0};
    char game_author[32] = {0};
    
    // 4. Run our custom reader to extract the target text strings
    parse_config(buffer, "title", game_title, sizeof(game_title));
    parse_config(buffer, "author", game_author, sizeof(game_author));
    
    // Free the temporary file text copy since we have the data extracted
    free(buffer);
    
    // 6. Direct command to change the host window frame title!
    SDL_SetWindowTitle(window, game_title);
}
