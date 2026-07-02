// mem.c
#include "mem.h"
#include <stdio.h>

uint8_t memory[RAM_SIZE] = {0};

uint8_t peek(uint32_t addr) { return addr < RAM_SIZE ? memory[addr] : 0; }
void poke(uint32_t addr, uint8_t val) { if (addr < RAM_SIZE) memory[addr] = val; }

uint16_t peek2(uint32_t addr) {
    return (addr + 1 < RAM_SIZE) ? *(uint16_t *)(memory + addr) : 0;
}
void poke2(uint32_t addr, uint16_t val) {
    if (addr + 1 >= RAM_SIZE) {
        fprintf(stderr, "[poke2] out-of-range addr=0x%X val=%u\n", addr, val);
        return;
    }
    *(uint16_t *)(memory + addr) = val;
}

uint32_t peek4(uint32_t addr) {
    return (addr + 3 < RAM_SIZE) ? *(uint32_t *)(memory + addr) : 0;
}
void poke4(uint32_t addr, uint32_t val) {
    if (addr + 3 < RAM_SIZE) *(uint32_t *)(memory + addr) = val;
}
