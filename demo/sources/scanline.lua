wiggly = {}

function wiggly.tick()
  	clear(0) -- clear the framebuffer
	
	-- scanline dist example. will have its own scene
	poke(0x0640D, 8) -- scanline amp
	poke(0x0640E, 4) -- scanline freq
  poke(0x0640F, (peek(0x0640F) + 2) % 256) -- scanline time
	
	-- simple text to test, might change to a sprite later on
	text("woeoeoah\n not too much tbh", 32, 43)

end
