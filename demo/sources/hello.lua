hello = {} -- make a table for the demo, this will reoccur in all the other demos

function hello.tick()
	clear(0) -- clear the framebuffer
		
	-- this renders the default palette
	for index = 0, 15 do -- a for loop that handles the palette index
		-- for each index, render the pixel at y = 38 and x = 32 but offset it by its own index
		pixel(32 + index, 38, index)
	end
	-- renders the text on the screen. "\n" prints it to the second line
	text("hello!\nwelcome to yf", 32, 43)
end
