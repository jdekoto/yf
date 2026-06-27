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

### MEMORY MAP
Since we are lacking documentation and everything works via peek/poke, here's the layout:
```c
#define ADDR_FB     0x00000u   /* 128×96 = (24KB)        */
#define ADDR_PAL    0x06000u   /* 512 slots = (1KB)      */
#define ADDR_INPUT  0x06440u   /* input state            */
#define ADDR_AUDIO  0x06450u   /* audio registers        */
#define ADDR_FONT   0x06600u   /* system font            */
#define ADDR_SPRB0  0x06900u   /* Sprite Bank 0: (8KB)   */
#define ADDR_SPRB1  0x08900u   /* Sprite Bank 1: (8KB)   */
#define ADDR_SNDBNK 0x10400u   /* sound bank (64KB)      */
#define ADDR_MAP    0x20400u   /* tilemap block (128KB)  */
#define ADDR_CART   0x40400u   /* cart RAM (~256KB)      */
#define ADDR_SRAM   0x7E000u   /* flash RAM (8KB)        */
```

### PROGRESS
As of right now, it is still in development, with api/tools and lack thereof, documentation, and overall guaranteed stability missing from the software. 

### ROADMAP
This is to monitor the software's progress:</br>
-- [WE ARE HERE] v0.0.9: almost everything in v0.1 but Docs</br>
-- v0.1: complete api, stable build, cassette format, and documentation</br>
-- v0.2: complete development environment + editors, raspi + mac build</br>
-- v0.3: homebrew platforms (3ds, vita, etc), possible dreamcast via ANTIRUINS engine</br>

### GRATITUDE
- ShrimpCatDev for heavy inspiration + palette and font via [CherryPop](https://github.com/ShrimpCatDev/CherryPop.git)
- Zep and his entire [Lexaloffle](www.lexaloffle.com) Trilogy for setting the standard of fantasy consoles
- Rxi for the heavy influence on the framework format via [cel7](https://rxi.itch.io/cel7)
- and Martin Cameron for the small MOD tracker library even we stopped using it via [micromod](https://github.com/martincameron/micromod)

