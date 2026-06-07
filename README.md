# yf
A creative sandbox with 512 kb of ram. 

<img width="480" height="318" alt="output" src="https://github.com/user-attachments/assets/80ce0237-3b1a-4a45-ad29-e4630aaf2c10" />

### DESC
This 16-bit fantasy console have all you need to make nostalgic games or works of art. It's modeled after the framework format, where it gains heavy inspiration from LOVE2D and cel7. The software includes:
- VM: Lua 5.4 (with hot reload on boot.lua only)
- RAM: 512 KB
- FB: 16-Bit Display with SQCIF resolution
- AUDIO: 4 Channels with a XM Hardware Tracker
- ROM: 16 MB Cassette Slot

### MEMORY MAP
Since we are lacking documentation and everything works via peek/poke, here's the layout:
```c
#define ADDR_FB     0x00000u   /* 128×96 = 12,288 bytes  */
#define ADDR_INPUT  0x06040u   /* input state            */
#define ADDR_AUDIO  0x06050u   /* audio registers        */
#define ADDR_FONT   0x06200u   /* system font            */
#define ADDR_SPRB0  0x06500u   /* Sprite Bank 0: 64x64   */
#define ADDR_SPRB1  0x08500u   /* Sprite Bank 1: 64x64   */
#define ADDR_SNDBUF 0x0A500u   /* audio stream buffer    */
#define ADDR_MAP    0x2A500u   /* tilemap vram block     */
#define ADDR_CART   0x3A500u   /* cart RAM (~270KB)      */
```

### PROGRESS
As of right now, it is still in development, with things like a cassette distribution format, api/tools and lack thereof, documentation, and overall guaranteed stability missing from the software. But to put it in simpler terms, it has everything you need to make a game in it (though depends on external tools and libraries.)

### ROADMAP
This is to monitor the software's progress:</br>
-- [WE ARE HERE] v0.0.7: somewhat complete api</br>
-- v0.1: complete api, stable build, cassette format and distribution</br>
-- v0.2: complete development environment + editors, raspi + mac build</br>
-- v0.3: homebrew platforms (3ds, vita, etc), possible dreamcast via ANTIRUINS engine</br>

### GRATITUDE
- ShrimpCatDev for heavy inspiration + palette and font via [CherryPop](https://github.com/ShrimpCatDev/CherryPop.git)
- Zep and his entire [Lexaloffle](www.lexaloffle.com) Trilogy for setting the standard of fantasy consoles
- Rxi for the heavy influence on the framework format via [cel7](https://rxi.itch.io/cel7)
- and Martin Cameron for the small XM tracker library via [ibxm](https://github.com/martincameron/micromod)

