maptest = {}

reload("assets/test.map", 0x30000)
sprsht("assets/tiles.bmp", 1)

cx = 0
cy = 0

function maptest.tick()
	cls(0)
	sbank(1)
	camera(cx, cy)
	
	if btn(4) then cx = cx - 1 end
	if btn(5) then cx = cx + 1 end
	-- Point the map renderer straight at column 55, row 80
	-- This keeps reading from the start of your map data (0,0),
	-- but pushes the rendering destination 16 pixels right and 8 pixels down!
	map(0, -2, 0, 0, 16, 12)
	text("press A/S to move camera", 4 + cx, 87 + cy, 13)
end
