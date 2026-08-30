# yf
A creative sandbox with 512 kb of ram. 

<img width="480" height="316" alt="debut" src="https://github.com/user-attachments/assets/47efea89-0c85-4521-ad08-10f246a7c44e" />

### DESC
This 16-bit fantasy console have all you need to make nostalgic games or works of art. It's modeled after the framework format, where it gains heavy inspiration from LOVE2D and cel7. The software includes:
- VM: Lua 5.4 CPU clocked at 6M ops / sec (with hot reload)
- RAM: 512 KB
- FB: 16-Bit Display with SQCIF resolution
- AUDIO: 4 Channels with BRR Decompression
- ROM: 16 MB Cassette Slot
- BUS: Unlimited Memcard Space with 2 Controllers

### MEMORY MAP
Since we are lacking documentation and everything works via peek/poke, here's the layout:
```c
#define ADDR_FB     0x00000u   /* 128×96 = (24KB)        */
#define ADDR_PAL    0x06000u   /* 512 slots = (1KB)      */
#define ADDR_INPUT  0x06440u   /* input state            */
#define ADDR_AUDIO  0x06450u   /* audio registers        */
#define ADDR_FONT   0x06600u   /* system font            */
#define ADDR_SPRB0  0x06900u   /* sprite bank 0: (8KB)   */
#define ADDR_SPRB1  0x08900u   /* sprite bank 1: (8KB)   */
#define ADDR_SNDBNK 0x10400u   /* sound bank (64KB)      */
#define ADDR_MAP    0x20400u   /* tilemap block (128KB)  */
#define ADDR_CART   0x40400u   /* cart RAM (~256KB)      */
#define ADDR_SRAM   0x7E000u   /* flash RAM (8KB)        */
```

### PROGRESS
Right now we have ALOT already implemented. With an actual ROM format, majority of the api giving you the capabilites to do whatever you want basically, all thats missing is documentation and a few bugs to iron out.</br>

### ROADMAP
We have a few things left before we distribute binaries and call it wraps.</br>
This includes (though not in order):</br>
-- 2 Player Controller Support [DONE]</br>
-- A proper appending method for fused binaries [DONE BUT CARRIES ROM BUGS]</br>
-- Mac Support [DONE BUT NEEDS TESTING]</br>
-- Software Map Layers [IMPLEMENTED BUT NOT TESTED/CONFIRMED]</br>
-- Flesh out the Sequencer a little bit more for full compatibility for MOD FX</br>
-- A few CLI tools for reverse engineering and asset conversion (in Nim this time)</br>
-- Fix load_sram() bug for ROMS</br>
-- Complete Demoscenes Example using ALL of the hardware.</br>
-- a complete and extensive documentation</br>

### GRATITUDE
- ShrimpCatDev for heavy inspiration + palette and font via [CherryPop](https://github.com/ShrimpCatDev/CherryPop.git)
- Zep and his entire [Lexaloffle](www.lexaloffle.com) Trilogy for setting the standard of fantasy consoles
- Rxi for the heavy influence on the framework format via [cel7](https://rxi.itch.io/cel7)
- and Martin Cameron for the small MOD tracker library even we stopped using it via [micromod](https://github.com/martincameron/micromod)

