
dots3d = {}

-- 3d dot party
-- by zep
-- holy moly yo
-- yea i have no idea what brodie did.
-- strenuous test on the cpu

local function all(t)
    local i = 0
    return function()
        i = i + 1
        return t[i]
    end
end

-- absolute ass circfill
function circfill(cx, cy, r, col)
    if r < 0 then return end
    local x, y, err = r, 0, 1 - r
    while x >= y do
        rect(cx - x, cy + y, x * 2 + 1, 1, col)
        rect(cx - x, cy - y, x * 2 + 1, 1, col)
        rect(cx - y, cy + x, y * 2 + 1, 1, col)
        rect(cx - y, cy - x, y * 2 + 1, 1, col)
        y = y + 1
        if err < 0 then err = err + 2 * y + 1
        else x = x - 1 err = err + 2 * (y - x) + 1 end
    end
end

-- rotate point x,y by a
-- (rotates around 0,0)
function rot(x,y,a)
	local x0=x
	x = math.cos(a)*x - math.sin(a)*y
	y = math.cos(a)*y + math.sin(a)*x0 -- *x is wrong but kinda nice too
	return x,y
end
	
local dt = 0
function dots3d.tick()
	clear(0)
	dt += 0.003
	if (not pt) then
		-- make some points
		pt={}
		for y=-1,1,1/2 do
			for x=-1,1,1/2 do
				for z=-1,1,1/2 do
					p={}
					p.x=x p.y=y p.z=z
					p.col=1 + math.floor(x*2+y*3)%10
					table.insert(pt,p)
				end
			end
		end
	end
	for p in all(pt) do
		--transform:
		--world space -> camera space
		
		p.cx,p.cz=rot(p.x,p.z,dt*4)
		p.cy,p.cz=rot(p.y,p.cz,dt*2.5)
		
		p.cz = p.cz + 2 + math.cos(dt*2.5)
	end
	
	-- sort furthest -> closest
	-- (so that things in distance
	-- aren't drawn over things
	-- in the foreground)
	
	for pass=1,4 do
	for i=1,#pt-1 do
		if pt[i].cz < pt[i+1].cz then
			--swap
			pt[i],pt[i+1]=pt[i+1],pt[i]
		end
	end
	for i=#pt-1,1,-1 do
		if pt[i].cz < pt[i+1].cz then
			--swap
			pt[i],pt[i+1]=pt[i+1],pt[i]
		end
	end
	end
	
	rad1 = 5+math.cos(dt*2)*4
	for p in all(pt) do
		--transform:
		--camera space -> screen space
		sx = 64 + p.cx*64/p.cz
		sy = 45 + p.cy*64/p.cz
		rad= rad1/p.cz
		-- draw
		if (p.cz > .1) then
			circfill(sx,sy,rad,p.col)
			circfill(sx+rad/3,sy-rad/3,rad/3,13)
		end
	end
	
	text("a zep demo", 4, 87, 13)
end
