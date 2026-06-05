
sound = {}

module_load("assets/heartbeats.xm")
module_play()

local wave_time = 0
-- Hardware Addresses from your architecture definitions
local ADDR_SNDBUF = 0x0A500
local ADDR_AUDIO  = 0x06050

function sound.tick()
    cls(0)
    module_tick()
	
	-- 1. Read the live 16-bit playhead indices directly from the SPU, only the left channel.
    local playhead_ch0 = peek(ADDR_AUDIO + 0x30) | (peek(ADDR_AUDIO + 0x31) << 4)

    -- 2. Draw across the screen width
    for x = 0, 128 do
        -- Use a modulo (%) operation to keep our cursors wrapping inside the 32KB bounds
        local sample_addr  = ADDR_SNDBUF + 0x0000 + ((playhead_ch0 + x) % 0x8000)

        local raw_sample  = peek(sample_addr)

        -- Normalize and format audio waves (-1.0 to 1.0)
        local wave = (raw_sample  - 128) / 128

        local wave_center  = 44
        local wave_amplitude = 40

        local pixel_y  = flr(wave_center  + (wave * wave_amplitude))

		local wave_color = (flr(x / 8) % 15) + 1	
		
		pset(x, pixel_y, wave_color)
    end
	
    text("press A/S to pause/play", 4, 80, 13)
    text("press enter to play sfx", 4, 87, 13)
	
    if btnp(BTN_ENTER) then sfx("assets/doop.wav", 1, 3) end
    if btnp(BTN_A) then module_pause() end
    if btnp(BTN_B) then module_play() end
	
end
