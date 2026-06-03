
y = 38

function render_pal()
	pset(32, y, 0)
	pset(33, y, 1)
	pset(34, y, 2)
	pset(35, y, 3)
	pset(36, y, 4)
	pset(37, y, 5)
	pset(38, y, 6)
	pset(39, y, 7)
	pset(40, y, 8)
	pset(41, y, 9)
	pset(42, y, 10)
	pset(43, y, 11)
	pset(44, y, 12)
	pset(45, y, 13)
	pset(46, y, 14)
	pset(47, y, 15)
end

-- A lightweight Lua synthesis utility that mimics Picotron's flexibility!
function synth_square_note(channel, frequency, duration_frames, volume)
    local sample_rate = 22050
    -- Calculate total byte length required for the note duration
    local total_bytes = math.floor((duration_frames / 60) * sample_rate)
    
    -- We will write the procedural wave into an isolated scratchpad buffer inside RAM
    local BUFFER_START = 0x0A000 
    
    local phase = 0
    local phase_inc = (2 * math.pi * frequency) / sample_rate
    
    for i = 0, total_bytes - 1 do
        phase = phase + phase_inc
        
        -- Calculate Square Wave value (either high or low)
        local value = 128
        if math.sin(phase) > 0 then
            value = 200 -- Wave crest
        else
            value = 56  -- Wave trough
        end
        
        -- Poke the generated PCM sample directly into the engine's RAM map!
        poke(BUFFER_START + i, value)
    end
    
    -- Tell the universal hardware player to immediately stream our freshly generated buffer!
    play_pcm(channel, BUFFER_START, total_bytes, false, volume)
end

function _tick()
	
	cls(0)
	render_pal()
	text("hello!\nwelcome to yf", 32, 43, 13)
	if btnp(BTN_LEFT) or btnp(BTN_RIGHT) or btnp(BTN_UP) or btnp(BTN_DOWN) then
		print("i can hear you")
	end
	if btnp(BTN_A) or btnp(BTN_B) or btnp(BTN_C) or btnp(BTN_D) or btnp(BTN_ENTER) then
		print("if you grab me imma bite you")
	end
	-- synth_square_note(0, 440, 22050, 255)
	
end
