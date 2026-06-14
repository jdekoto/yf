
flashrom = {}

-- create the save file.
flash('testfire')

score = 0

function flashrom.tick()
	cls(0)
	-- setup the cart index. currently the max cart index is 64.
	highscore = fget(0)
	
	-- simple button masher
	if btnp(4) then score += 1 end
	
	-- if we passed the highscore and user presses S:
	if score > highscore and btnp(5) then
		highscore = score
		fset(0, highscore)
	end
	
	
	text("highscore:".. highscore, 4, 4, 13)
	text("press S to save", 4, 11, 13)
	text("press A to score", 4, 80, 13)
	text("score:" .. score, 4, 87, 13)

end
