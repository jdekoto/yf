
#ifndef MEM_H
#define MEM_H

#include <stdint.h>

#define RAM_SIZE   (1024 * 1024)

#define ADDR_FB     0x00000u   /* 128×96 = 12,288 bytes  */
#define ADDR_PAL    0x03000u   /* 16 colors × 4 bytes    */
#define ADDR_INPUT  0x03040u   /* input state            */
#define ADDR_AUDIO  0x03050u   /* audio registers        */
#define ADDR_FONT   0x03200u   /* system font            */
#define ADDR_SNDBUF 0x03500u   /* audio stream buffer    */
#define ADDR_CART   0x04000u   /* cart RAM (~786KB)      */

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

typedef uint32_t Pixel;

// bummy bitshifter
#define RGBA(r,g,b,a) (((uint32_t)(r)<<24)|((uint32_t)(g)<<16)|((uint32_t)(b)<<8)|(a))

extern uint8_t memory[RAM_SIZE];

uint8_t  peek (uint32_t addr);
void     poke (uint32_t addr, uint8_t  val);
uint16_t peek2(uint32_t addr);
void     poke2(uint32_t addr, uint16_t val);
uint32_t peek4(uint32_t addr);
void     poke4(uint32_t addr, uint32_t val);

void    fb_expand(Pixel *dst);
void    fb_cls   (uint8_t color);
void    fb_pset  (int x, int y, uint8_t color);
uint8_t fb_pget  (int x, int y);

#endif


