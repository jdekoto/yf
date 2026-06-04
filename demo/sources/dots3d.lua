
dots3d = {}

-- 3d dot party
-- by zep

local function all(t)
    local i = 0
    return function()
        i = i + 1
        return t[i]
    end
end

-- rotate point x,y by a
-- (rotates around 0,0)
function rot(x,y,a)
	local x0=x
	x = cos(a)*x - sin(a)*y
	y = cos(a)*y + sin(a)*x0 -- *x is wrong but kinda nice too
	return x,y
end
	
function dots3d.tick()
	cls(0)
	if (not pt) then
		-- make some points
		pt={}
		for y=-1,1,1/2 do
			for x=-1,1,1/2 do
				for z=-1,1,1/2 do
					p={}
					p.x=x p.y=y p.z=z
					p.col=1 + flr(x*2+y*3)%10
					table.insert(pt,p)
				end
			end
		end
	end
	for p in all(pt) do
		--transform:
		--world space -> camera space
		
		p.cx,p.cz=rot(p.x,p.z,t()*2)
		p.cy,p.cz=rot(p.y,p.cz,t()*1.25)
		
		p.cz = p.cz + 2 + cos(t()*1.25)
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
	
	rad1 = 5+cos(t()*2)*4
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
