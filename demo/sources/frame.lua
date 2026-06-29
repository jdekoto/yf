
frame = {}

-- draws frame btw
function wireframe(x, y, w, h, col)
    if w <= 0 or h <= 0 then return end
    rect(x, y, w, 1, col)                 -- Top edge
    rect(x, y + h - 1, w, 1, col)         -- Bottom edge
    rect(x, y + 1, 1, h - 2, col)         -- Left edge
    rect(x + w - 1, y + 1, 1, h - 2, col) -- Right edge
end

function circ(cx, cy, r, col)
    -- Handle the trivial radius 0 case (single pixel)
    if r == 0 then
        pixel(cx, cy, col)
        return
    end

    local x = 0
    local y = r
    local d = 3 - (2 * r) -- Initial decision parameter

    -- Helper local function to mirror the 8 symmetrical octants cleanly
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

local dt = 0
function frame.tick()
	clear(0)
	dt += 0.001
	-- peekaboo type shi
	local x = 64
	x += math.sin(dt*16)*40
	
	clip(28, 16, 71, 60)
	-- draw circle in clip frame.
	circ(x, 45, 6, 13)
	-- reset clipping to screen
	clip()
	-- draw frame outside out of clip frame
	wireframe(26, 16, 75, 61, 15)	
	-- since its a modern art piece
	text("peekaboo!", 26, 79)
	text("clip demo", 26, 85)
end
