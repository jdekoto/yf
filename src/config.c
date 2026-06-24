#include "config.h"

// this is both for the window title and the cart header

void parse_config(const char *file_content, const char *key, char *output, int max_len) {
    const char *line = file_content;
    
    while (line && *line) {
        // Check if the current line starts with our key word
        if (strncmp(line, key, strlen(key)) == 0) {
            const char *value_ptr = line + strlen(key);
            
            // Skip past any extra spaces, tabs, or '=' characters to find the raw text value
            while (*value_ptr && (*value_ptr == '=' || *value_ptr == ' ' || *value_ptr == '\t')) {
                value_ptr++;
            }
            
            int i = 0;
            while (*value_ptr && i < (max_len - 1)) {
                // backslash continuation
                if (*value_ptr == '\\') {
                    const char *next = value_ptr + 1;
                    
                    // Peek ahead past potential Windows (\r\n) or Linux (\n) line endings
                    if (*next == '\r') next++;
                    if (*next == '\n') {
                        next++; // Move past the newline onto the next line
                        
                        // Automatically skip leading indent spaces/tabs on the next line
                        while (*next == ' ' || *next == '\t') {
                            next++;
                        }
                        
                        value_ptr = next; // Advance our main pointer to the stitched position
                        
                        // Inject a separating space between tokens if needed
                        if (i > 0 && output[i - 1] != ' ' && i < (max_len - 1)) {
                            output[i++] = ' ';
                        }
                        continue; // Jump back to reading characters
                    }
                }
                
                // Stop if we hit a true line break (not escaped by a backslash)
                if (*value_ptr == '\n' || *value_ptr == '\r') {
                    break;
                }
                
                output[i++] = *value_ptr++;
            }
            
            // Trim trailing spaces that might have sneaked in right before the line ended
            while (i > 0 && isspace((unsigned char)output[i - 1])) {
                i--;
            }
            
            output[i] = '\0'; // Always null-terminate your string!
            return;
        }
        
        // Move to the next line by looking for the next newline character
        line = strchr(line, '\n');
        if (line) line++;
    }
    
    // Fallback if the key was completely missing from the file
    strncpy(output, "Untitled", max_len - 1);
    output[max_len - 1] = '\0';
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
