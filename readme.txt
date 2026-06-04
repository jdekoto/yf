yf

a framework based fantasy console

specs:
  lua 5.4
  1 mb memory
  16-bit display

how to use:
  simply go into terminal and type:
    ./yf <project_folder> 
  ex: ./yf demo

warning:
  this is not a finished project
  but just to let you know, keep your 
  projects under 16 mb. a cassette
  format will come up soon.

since there's little to no api, almost everything
can work through peek and poke:
#define ADDR_FB     0x00000u   /* 128×96 = 12,288 bytes  */
#define ADDR_INPUT  0x06040u   /* input state            */
#define ADDR_AUDIO  0x06050u   /* audio registers        */
#define ADDR_FONT   0x06200u   /* system font            */
#define ADDR_CART   0x06500u   /* cart RAM (~870KB)      */
#define ADDR_SNDBUF 0xE0000u   /* audio stream buffer    */

appreciation:
i wanna say a huge thanks to the CherryPop
developer as that was a major creative influence.
Go support and check them out: https://github.com/ShrimpCatDev/CherryPop
(also thanking them cuz lowk using their font/palette right now)

next i would also wanna say a huge thanks to
the ibxm developer as well, it shows that i dont need
an additional 382 mb to make a hardware tracker. (copyright in vendored files)

finally i wanna thank all of lexaloffle who basically 
started this genre and defined the fantasy blueprint. 

alr thats it. bye-bye :)
