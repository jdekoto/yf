#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void parse_config(const char *file_content, const char *key, char *output, int max_len);

#endif
