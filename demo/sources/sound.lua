
sound = {}

module_load("assets/roundii.xm")
module_play()

local wave_time = 0

function sound.tick()
    cls(0)
    module_tick()
	
    wave_time = wave_time + 0.15
    
    local wave_center_y = 44 -- Vertically center the wave on screen
    local wave_amplitude = 12 -- How tall the wave peaks are

    for x = 0, 160 do
        local sine_val = sin((x * 0.15) + wave_time)
        local cos_val  = cos((x * 0.08) - (wave_time * 0.5))
        
        local y_offset = flr((sine_val + cos_val * 0.3) * wave_amplitude)
        local pixel_y = wave_center_y + y_offset
        
        local wave_color = flr(x / 8) % 16

        line(x - 8, wave_center_y, x, pixel_y, wave_color)
    end
	
    text("press A/S to pause/play", 4, 80, 13)
    text("press enter to play sfx", 4, 87, 13)
	
    if btnp(BTN_ENTER) then sfx("assets/doop.wav", 1, 2) end
    if btnp(BTN_A) then module_pause() end
    if btnp(BTN_B) then module_play() end
	
end
