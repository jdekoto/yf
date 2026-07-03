tilemap = {}
-- TODO: redo the tilemap and the map data to where we can test out the map layer system

memcpy(0x1E900, include('assets/map/test.map'))
memcpy(0x0A900, include('assets/sprites/tiles.raw'))

cx = 0
cy = 0

function tilemap.tick()
	clear(0)
	poke(0x06409, 1)   -- switch to sprite bank 1
	camera(cx, cy)
	
	if btn(4) then cx -= 1 end
	if btn(5) then cx += 1 end
	-- Point the map renderer straight at column 55, row 80
	-- This keeps reading from the start of your map data (0,0),
	-- but pushes the rendering destination 16 pixels right and 8 pixels down!
	map(0, -2, 0, 0)
	text("128kb chunk map", 4 + cx, 4)
	text("press A/S to move camera", 4 + cx, 87)
end
