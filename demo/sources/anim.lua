anim = {}

reload("assets/sprites/sprites.raw", 0x06900)

-- coords
local f  = 0
local py = 78
local dy = 0

function anim.tick()
    clear(0)
    poke(0x06400, 0)   -- switch to sprite bank 0

    -- jump if player is sitting on the floor
    if py >= 78 and btn(4) then
    		sound(1, 100)
        dy -= 2.8 -- jumping
    end

    -- 2.velocity/gravity
    py += dy
    dy += 0.15

    -- 3. fake collison, sits right above the text
    if py >= 78 then
        -- halt downward acceleration
        py = 78
        dy = 0
        f = 0
    else
        -- change frame based on its velocity
        if dy < 0 then
            f = 8 -- jump
        else
            f = 9 -- fall
        end
    end
	
	-- draw the sprite
    sprite(f, 60, py) 

	-- draw text
    text(string.format("dy: %.2f", dy), 4, 4, 13)
    text("press A to jump", 4, 87, 13)
end
