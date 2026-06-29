-- includes all the demo scenes into the cassttes entry file
require("hello")
require("anim")
require("audio")
require("dots3d")
require("frame")
require("flashrom")
require("map")

-- main initalization function
function _boot()
	-- load the cassette's sndbnk into its dedicated ram block
	reload("assets/sfx/soundbank.bin", 0x0E900)

	-- based on ANTIRUINS' scene example, make a table of states for each demo
	states = { "hello", "anim", "map", "audio", "frame", "flashrom", "dots3d" }
	-- or current state, for right now a meaningless number. but it will set the current
	-- scene by referring to the index of the state table
	cState = 1
end

-- main tick loop
function _tick()
	-- handles scene switching and loops the index when we run out of demos in the table
	if btnp(0) then
		cState -= 1
		if cState < 1 then
			cState = #states
		end
		-- just to be safe we clear the screen for the next demo, plus add a lil sfx :)
		clear(0)
		sound(0, 128)
	elseif btnp(1) then
		cState += 1
		if cState > #states then
			cState = 1
		end
		-- pretty much doing the same thing yo.
		clear(0)
		sound(0, 128)
	end

	-- now we map the index to the states, if it equals a certain demo, then run its corresponding tick function.
	local mode = states[cState]
	if mode == "hello" then
		hello.tick()
	elseif mode == "anim" then
		anim.tick()
	elseif mode == "audio" then
		audio.tick()
	elseif mode == "dots3d" then
		dots3d.tick()
	elseif mode == "frame" then
		frame.tick()
	elseif mode == "flashrom" then
		flashrom.tick()
	elseif mode == "map" then
		maptest.tick()
	end

	-- reset the camera for the map since its a global hardware offset
	if mode == "map" then
		camera()
	end
end
