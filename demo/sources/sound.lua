
sound = {}

Tracker.load("assets/roundii.xm")
Tracker.play()

function sound.tick()
	cls(0)
	Tracker.update()
	text("press A/S to pause/play", 8, 80, 13)
	text("press enter to play sfx", 8, 87, 13)
	
	if btnp(BTN_ENTER) then
		sfx("assets/doop.wav", 64, 2)
	end
	if btnp(BTN_A) then
		Tracker.pause()
	end
	if btnp(BTN_B) then
		Tracker.play()
	end
	
end
