--[[local t = 0
function circfill(cx, cy, r, col)
  if r < 0 then
    return
  end
  local x, y, err = r, 0, 1 - r
  while x >= y do
    rect(cx - x, cy + y, x * 2 + 1, 1, col)
    rect(cx - x, cy - y, x * 2 + 1, 1, col)
    rect(cx - y, cy + x, y * 2 + 1, 1, col)
    rect(cx - y, cy - x, y * 2 + 1, 1, col)
    y = y + 1
    if err < 0 then
      err = err + 2 * y + 1
    else
      x = x - 1
      err = err + 2 * (y - x) + 1
    end
  end
end

function _tick()
  clear(0)
  t = t + 2.00
  for i = 32, 1, -1 do
    local r = i * 4
    local wave_x = math.sin(t + i * 0.1) * 15
    local wave_y = math.cos(t * 0.8 + i * 0.15) * 10
    local pulse = r + math.sin(t * 2 + i * 0.3) * 6
    local col = 1 + math.floor((i + t * 10) % 15)
    circfill(64 + math.floor(wave_x), 44 + math.floor(wave_y), math.floor(pulse), col)
  end
  rect(36, 42, 55, 9, 511)
  text("no cassette", 38, 44, 13)
end
]]
sprites = include('sprites.raw')
memcpy(0x06900, sprites, 981)

function _tick()
	sprite(1, 24, 13)
end
