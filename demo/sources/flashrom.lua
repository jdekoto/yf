
flashrom = {}

score = 0
r = 6

-- We check for a magic validation byte (0xc8) at the very start of sram
if peek(0x7E000) == 0xc8 then
    -- if we already have a save then set it to the highscore
    highscore = peek(0x7E000 + 2)
else
    -- if we dont then intialize it
    poke(0x7E000, 0xc8)
    poke(0x7E000 + 2, 0)
    highscore = 0
end

-- yes same circ function
function circ(cx, cy, r, col)
    -- Handle the trivial radius 0 case (single pixel)
    if r == 0 then
        pixel(cx, cy, col)
        return
    end

    local x = 0
    local y = r
    local d = 3 - (2 * r) -- Initial decision parameter

    -- helper local function to mirror the 8 symmetrical octants cleanly
    local function plot8(x, y) 		
        pixel(cx + x, cy + y, col)
        pixel(cx - x, cy + y, col)
        pixel(cx + x, cy - y, col)
        pixel(cx - x, cy - y, col)
        pixel(cx + y, cy + x, col)
        pixel(cx - y, cy + x, col)
        pixel(cx + y, cy - x, col)
        pixel(cx - y, cy - x, col)
    end

    plot8(x, y)

    while x <= y do
        x = x + 1       
        if d > 0 then
            y = y - 1
            d = d + 4 * (x - y) + 10
        else
            d = d + (4 * x) + 6
        end
        plot8(x, y)
    end
end

function flashrom.tick()
	clear(0)
	
	-- simple button masher
	if btnp(4) then score += 1 r += 12 sound(3, 200) end
	
	-- slowly shrink back down to the baseline every frame
    if r > 6 then
        r -= 0.6
        if r < 6 then 
            r = 6
        end
    end
	
	-- if we passed the highscore and user presses S
	if score > highscore and btnp(5) then
		-- sets highscore
		highscore = score
		--- pokes it into sram
		poke(0x7E000 + 2, highscore)
	end
	
	-- visual candy
	circ(64, 44, math.floor(r), 15)
	
	-- text
	text("highscore:".. highscore, 4, 4)
	text("press S to save", 4, 11)
	text("press A to score", 4, 80)
	text("score:" .. score, 4, 87)

end
