
frame = {}

function frame.tick()
	cls(0)
	local x = 64
	x = x + sin(t()*8)*32
	
	clip(64, 16, 36, 60)
	
	circ(x, 45, 6, 13)
	
	
	clip()
	
	rect(64, 16, 100, 76, 7)
	
	text("clipping works", 4, 87, 13)
end
