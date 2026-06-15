#ifndef RUNTIME_H
#define RUNTIME_H

const char* BIOS =
"local t = 0\n"
"function visuals()\n"
    "t = t + 0.0002\n"
    "local cx, cy = 64, 44\n"
    "for i = 32, 1, -1 do\n"
        "local r = i * 4\n"
        "local wave_x = sin(t + i * 0.1) * 15\n"
        "local wave_y = cos(t * 0.8 + i * 0.15) * 10\n"
        "local pulse = r + sin(t * 2 + i * 0.3) * 6\n"
        "local col = 1 + flr((i + t * 10) % 15)\n"
        "circfill(cx + flr(wave_x), cy + flr(wave_y), flr(pulse), col)\n"
    "end\n"
"end\n"
"function _tick()\n"
	"cls(0)\n"
	"visuals()\n"
	"local black = rgb(0,0,0)\n"
	"rectfill(36, 42, 89, 50, black)\n"
	"text(\"no cassette\", 38, 44, 13)\n"
"end\n";

#endif
