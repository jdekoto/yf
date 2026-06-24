
#ifndef MEM_H
#define MEM_H

#include <stdint.h>

#define RAM_SIZE   (512 * 1024)

#define ADDR_FB     0x00000u   /* 128×96 =  24,576 bytes */
#define ADDR_INPUT  0x06040u   /* input state            */
#define ADDR_AUDIO  0x06050u   /* audio registers        */
#define ADDR_FONT   0x06200u   /* system font            */
#define ADDR_SPRB0  0x06500u   /* Sprite Bank 0: 128x64  */
#define ADDR_SPRB1  0x08500u   /* Sprite Bank 1: 128x64  */
#define ADDR_SNDBNK 0x10000u   /* sound bank (64KB)      */
#define ADDR_MAP    0x20000u   /* tilemap block (128KB)  */
#define ADDR_CART   0x40000u   /* cart RAM (~256KB)      */

#define FB_WID 128
#define FB_HEI  96

#define CH_STATUS(ch)   (ADDR_AUDIO + ((ch) * 10) + 0) // 0 = off, 1 = PCM, 2 = BRR
#define CH_TRIGGER(ch)  (ADDR_AUDIO + ((ch) * 10) + 1) // 1 = Trigger reset
#define CH_LOOP(ch)     (ADDR_AUDIO + ((ch) * 10) + 2) // 0 = play once, 1 = loop
#define CH_ADDR_LO(ch)  (ADDR_AUDIO + ((ch) * 10) + 3) // Sample offset low byte
#define CH_ADDR_HI(ch)  (ADDR_AUDIO + ((ch) * 10) + 4) // Sample offset high byte
#define CH_LEN_LO(ch)   (ADDR_AUDIO + ((ch) * 10) + 5) // Sample length low byte
#define CH_LEN_HI(ch)   (ADDR_AUDIO + ((ch) * 10) + 6) // Sample length high byte
#define CH_VOLUME(ch)   (ADDR_AUDIO + ((ch) * 10) + 7) // 0 to 255
#define CH_PITCH(ch)    (ADDR_AUDIO + ((ch) * 10) + 8) // 256 = 1.0 speed
#define CH_BUF_HALF(ch) (ADDR_AUDIO + ((ch) * 10) + 9) // Exposes half the buffer
#define ADDR_TRACKER_ENABLED (ADDR_AUDIO + 0x42)  // 1-byte toggle (0 = Off, 1 = On)
#define ADDR_TRACKER_VOLUME  (ADDR_AUDIO + 0x43)  // 1-byte master gain (0 to 255)

extern uint8_t memory[RAM_SIZE];

uint8_t  peek (uint32_t addr);
void     poke (uint32_t addr, uint8_t  val);
uint16_t peek2(uint32_t addr);
void     poke2(uint32_t addr, uint16_t val);
uint32_t peek4(uint32_t addr);
void     poke4(uint32_t addr, uint32_t val);

void    fb_expand(uint16_t *dst);

#endif


