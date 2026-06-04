
hello = {}

y = 38

function render_pal() -- horrific function ik
	pset(32, y, 0)
	pset(33, y, 1)
	pset(34, y, 2)
	pset(35, y, 3)
	pset(36, y, 4)
	pset(37, y, 5)
	pset(38, y, 6)
	pset(39, y, 7)
	pset(40, y, 8)
	pset(41, y, 9)
	pset(42, y, 10)
	pset(43, y, 11)
	pset(44, y, 12)
	pset(45, y, 13)
	pset(46, y, 14)
	pset(47, y, 15)
end

sprsht("assets/sprites.bmp", 0x04000)

function hello.tick()
	
	cls(0)
	render_pal()
	spr(1, 29, 26) 
	text("hello!\nwelcome to yf", 32, 43, 13)
	
end
