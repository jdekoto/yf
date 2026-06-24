
require('hello')
require('anim')
require('sound')
require('dots3d')
require('frame')
require('flashrom')
require('map')

-- load the cassette's sndbnk into ram
reload("assets/sfx/soundbank.bin", 0x10000)

-- based on ANTIRUINS' scene example
states = {"hello", "anim", "map", "sound", "frame", "flashrom", "dots3d"}
cState = 1 

function _tick()

	if btnp(0) then
		cState = cState - 1
        	if cState < 1 then cState = #states end
        	cls(0)
        	sfx(0, 128)
	elseif btnp(1) then
		cState = cState + 1
		if cState > #states then cState = 1 end
		cls(0)
		sfx(0, 128)
	end
	
	local mode = states[cState]
	if mode == "hello" then 
		hello.tick()
	elseif mode == "anim" then
		anim.tick()
	elseif mode == "sound" then
		sound.tick()
	elseif mode == "dots3d" then
		dots3d.tick()
	elseif mode == "frame" then
		frame.tick()
	elseif mode == "flashrom" then
		flashrom.tick()
	elseif mode == "map" then
		camera(0,0)
		maptest.tick()
	end
	
	-- reset the camera for the map since its lowk global
	if (mode == "map") then 
		camera()
	end
	
end
