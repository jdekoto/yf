
hello = {}

y = 38

function render_pal()
    for index = 0, 15 do
        pset(32 + index, y, index)
    end
end

sprsht("assets/sprites.bmp", 0)

function hello.tick()
	cls(0)
	render_pal()
	spr(0, 29, 26) 
	text("hello!\nwelcome to yf", 32, 43, 13)
end
