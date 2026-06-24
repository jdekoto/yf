maptest = {}

reload("assets/map/test.map", 0x20000)
sprsht("assets/sprites/tiles.raw", 64, 16, 1)

cx = 0
cy = 0

function maptest.tick()
	cls(0)
	bank(1)
	camera(cx, cy)
	
	if btn(4) then cx = cx - 1 end
	if btn(5) then cx = cx + 1 end
	-- Point the map renderer straight at column 55, row 80
	-- This keeps reading from the start of your map data (0,0),
	-- but pushes the rendering destination 16 pixels right and 8 pixels down!
	map(0, -2, 0, 0)
	text("128kb chunk map", 4 + cx, 4, 13)
	text("press A/S to move camera", 4 + cx, 87, 13)
end
