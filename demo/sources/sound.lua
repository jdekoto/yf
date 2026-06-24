
sound = {}

-- overall background music. 
mus = module("assets/arp.cm", 0.8)
-- mus.play()

function sound.tick()
    cls(0)
	-- ─── NEW CLEAN OSCILLOSCOPE VISUALIZER ───
    -- Draw across the 128-pixel screen width using our live C output buffer
    for x = 0, 127 do
        -- Read the raw mixed sample directly from the visualizer window
        local raw_sample = peek(ADDR_AUDIO + 0x40 + x)

        -- Normalize audio amplitude down from an unsigned byte range (0 to 255)
        local wave = (raw_sample - 128) / 128

        local wave_center    = 44
        local wave_amplitude = 32
        local pixel_y        = flr(wave_center + (wave * wave_amplitude))

        -- Dynamic color cycling effect across columns
        local wave_color = (flr(x / 8) % 15) + 1	
        
        pset(x, pixel_y, wave_color)
    end
    text("press A/S to pause/play", 4, 80, 13)
    text("press enter to play sfx", 4, 87, 13)
	
    if btnp(8) then sfx(2, 128) end
    --if btnp(4) then mus.pause() end
    --if btnp(5) then mus.play() end
	
end
