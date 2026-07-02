
#ifndef MEM_H
#define MEM_H

#include <stdint.h>

#define RAM_SIZE   (512 * 1024)

#define ADDR_FB     0x00000u   /* 128×96 = (24KB)        */
#define ADDR_PAL    0x06000u   /* 512 slots = (1KB)      */
#define ADDR_REGS   0x06400u   /* base for the registers */
#define ADDR_INPUT  0x06440u   /* input state            */
#define ADDR_AUDIO  0x06450u   /* audio registers        */
#define ADDR_FONT   0x06600u   /* system font            */
#define ADDR_SPRB0  0x06900u   /* Sprite Bank 0: (16KB)  */
#define ADDR_SPRB1  0x0A900u   /* Sprite Bank 1: (16KB)  */
#define ADDR_SNDBNK 0x0E900u   /* sound bank (64KB)      */
#define ADDR_MAP    0x1E900u   /* tilemap block (128KB)  */
#define ADDR_CART   0x3E900u   /* cart RAM (~256KB)      */
#define ADDR_SRAM   0x7E000u   /* flash RAM (8KB)        */

#define FB_WID 128
#define FB_HEI  96

// hardware regs (mostly audio)
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
#define TRACKER_ENABLED (ADDR_AUDIO + 0x42)            // 1-byte toggle (0 = Off, 1 = On)
#define TRACKER_VOLUME  (ADDR_AUDIO + 0x43)            // 1-byte master gain (0 to 255)

#define REG_CAM_X       (ADDR_REGS + 0)  /* Camera X Offset (16-bit signed, 2 bytes) */
#define REG_CAM_Y       (ADDR_REGS + 2)  /* Camera Y Offset (16-bit signed, 2 bytes) */

#define REG_CLIP_EN     (ADDR_REGS + 4)  /* Clipping Engine Toggle (8-bit, 1 byte) */
#define REG_CLIP_X0     (ADDR_REGS + 5)  /* Clip Rect Left (8-bit, 1 byte) */
#define REG_CLIP_Y0     (ADDR_REGS + 6)  /* Clip Rect Top (8-bit, 1 byte) */
#define REG_CLIP_X1     (ADDR_REGS + 7)  /* Clip Rect Right (8-bit, 1 byte) */
#define REG_CLIP_Y1     (ADDR_REGS + 8)  /* Clip Rect Bottom (8-bit, 1 byte) */

#define REG_BANK_SW     (ADDR_REGS + 9)  /* Sprite/Tile Bank Control Switcher */

#define REG_FILLP       (ADDR_REGS + 10) /* 4x4 Dither Pattern Mask (16-bit, 2 bytes) */
#define REG_FILLP_COLOR (ADDR_REGS + 12) /* Secondary Dither Color (8-bit, 1 byte) */

#define REG_WAVE_AMP    (ADDR_REGS + 13) /* Scanline Wave Distortion Amplitude (8-bit, 1 byte) */
#define REG_WAVE_FREQ   (ADDR_REGS + 14) /* Scanline Wave Distortion Frequency (8-bit, 1 byte) */
#define REG_WAVE_TIME   (ADDR_REGS + 15) /* Scanline Wave Distortion Time/Phase (8-bit, 1 byte) */

extern uint8_t memory[RAM_SIZE];

uint8_t  peek (uint32_t addr);
void     poke (uint32_t addr, uint8_t  val);
uint16_t peek2(uint32_t addr);
void     poke2(uint32_t addr, uint16_t val);
uint32_t peek4(uint32_t addr);
void     poke4(uint32_t addr, uint32_t val);

void    fb_expand(uint16_t *dst);

#endif


