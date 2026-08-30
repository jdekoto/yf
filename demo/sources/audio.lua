
audio = {}

-- overall background music that sucks cuz we dont have a tracker yet but wait till we do
--mus = module("assets/arp.cm", 0.8) 
--mus.play()

function audio.tick()
    clear(0)
    -- draw across the 128-pixel screen width using our live C output buffer
    -- under audio regs for this very reason. who knows, maybe it'll be per channel
    for x = 0, 127 do
        -- read the raw mixed sample directly from the visualizer window
        local raw_sample = peek(0x06450 + 0x50 + x)

        -- normalize audio amplitude down from an unsigned byte range (0 to 255)
        local wave = (raw_sample - 128) / 128

        local wave_center    = 44
        local wave_amplitude = 32
        local pixel_y        = math.floor(wave_center + (wave * wave_amplitude))

        -- dynamic color cycling effect across columns
        local wave_color = (math.floor(x / 8) % 15) + 1	
        
        pixel(x, pixel_y, wave_color)
    end
    text("press A/S to pause/play", 4, 80)
    text("press enter to play sfx", 4, 87)
	
    if btnp(8) then sound(2, 128) end
--    if btnp(4) then mus.pause() end
--    if btnp(5) then mus.play() end
	
end
