
#ifndef MEM_H
#define MEM_H

#include <stdint.h>

#define RAM_SIZE   (512 * 512)

#define ADDR_FB     0x00000u   /* 128×96 = 12,288 bytes  */
#define ADDR_INPUT  0x06040u   /* input state            */
#define ADDR_AUDIO  0x06050u   /* audio registers        */
#define ADDR_FONT   0x06200u   /* system font            */
#define ADDR_SPRB0  0x06500u   /* Sprite Bank 0: 64x64   */
#define ADDR_SPRB1  0x08500u   /* Sprite Bank 1: 64x64   */
#define ADDR_SNDBUF 0x0A500u   /* audio stream buffer    */
#define ADDR_CART   0x2A500u   /* cart RAM (~870KB)      */

#define FB_WID 128
#define FB_HEI  96

#define CH_STATUS(ch)  (ADDR_AUDIO + ((ch) * 10) + 0)
#define CH_TRIGGER(ch) (ADDR_AUDIO + ((ch) * 10) + 1)
#define CH_LOOP(ch)    (ADDR_AUDIO + ((ch) * 10) + 2)
#define CH_ADDR_0(ch)  (ADDR_AUDIO + ((ch) * 10) + 3) 
#define CH_ADDR_1(ch)  (ADDR_AUDIO + ((ch) * 10) + 4) 
#define CH_ADDR_2(ch)  (ADDR_AUDIO + ((ch) * 10) + 5) 
#define CH_LEN_0(ch)   (ADDR_AUDIO + ((ch) * 10) + 6)
#define CH_LEN_1(ch)   (ADDR_AUDIO + ((ch) * 10) + 7)
#define CH_LEN_2(ch)   (ADDR_AUDIO + ((ch) * 10) + 8)
#define CH_VOLUME(ch)  (ADDR_AUDIO + ((ch) * 10) + 9)
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


