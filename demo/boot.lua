
require('sources.hello')
require('sources.sound')
require('sources.dots3d')

-- based on ANTIRUINS' scene example
states = {"hello", "sound", "dots3d"}
cState = 1 

function _tick()
	if btnp(BTN_LEFT) then
		cState = cState - 1
        	if cState < 1 then cState = #states end
        	cls(0)
	elseif btnp(BTN_RIGHT) then
		cState = cState + 1
		if cState > 3 then cState = 1 end
		cls(0)
	end
	
	local mode = states[cState]
	--print("mode: %d ", mode)
	if mode == "hello" then 
		hello.tick()
	elseif mode == "sound" then
		sound.tick()
	elseif mode == "dots3d" then
		dots3d.tick()
	end
end
