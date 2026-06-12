
hello = {}

function render_pal()
    for index = 0, 15 do
        pset(32 + index, 38, index)
    end
end

function hello.tick()
	cls(0)
	render_pal()
	text("hello!\nwelcome to yf", 32, 43, 13)
end
