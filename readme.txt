yf

a framework based fantasy console

specs:
  lua 5.4
  1 mb memory

how to use:
  simply go into terminal and type:
    ./yf <project_folder> 
  ex: ./yf demo

warning:
  this is not a finished project
  but just to let you know, keep your 
  projects under 16 mb. a cassette
  format will come up soon.

since there's no api, almost everything
can work through peek and poke:
#define ADDR_FB     0x00000u   /* 128×96 = 12,288 bytes  */
#define ADDR_PAL    0x03000u   /* 16 colors × 4 bytes    */
#define ADDR_INPUT  0x03040u   /* input state            */
#define ADDR_AUDIO  0x03050u   /* audio registers        */
#define ADDR_FONT   0x03200u   /* system font            */
#define ADDR_SNDBUF 0x03500u   /* audio stream buffer    */
#define ADDR_CART   0x04000u   /* cart RAM (~786KB)      */
