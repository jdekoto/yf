-- simple copy/paste
-- we can pause and resume. there is SOME sort of rhythm to the sounds the pitches are
-- messed up, so the frequency conversion may be a factor. ill implement a click track
-- on one of the channels to see if the bpm is correctly converted

-- alr pitch now works. few things left. like the tempo/speed and volume maybe

-- overall background music that sucks cuz we dont have a tracker yet but wait till we do
mus = module(include('assets/novice.cm'), 1.0) 
mus.play()

memcpy(0x0E900, include('assets/sndbnk.bin'))

function _tick()
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
	text("press Z/X to fade in/out",4, 87)
	
    if btnp(4) then mus.pause() end
    if btnp(5) then mus.play() end
    if btnp(6) then mus.fade(0.2, 120) end
	if btnp(7) then mus.fade(0.6, 120) end
end
