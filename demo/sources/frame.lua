
frame = {}

function frame.tick()
	cls(0)
	-- peekaboo type shi
	local x = 64
	x = x + sin(t()*8)*40
	
	clip(28, 16, 72, 60)
	-- draw circle in clip frame.
	circ(x, 45, 6, 13)
	-- reset clipping to screen
	clip()
	-- draw frame outside out of clip frame
	rect(28, 16, 100, 76, 7)
	
	text("peekaboo!", 28, 79, 13)
	text("clip demo", 28, 85, 13)
end
