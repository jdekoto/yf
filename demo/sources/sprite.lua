sprite = {}

sprsht("assets/sprites.bmp", 0)

-- Define Sprite Sheet Frame IDs
local FRAME_GROUND  = 0  -- Idle / Standing
local FRAME_JUMPING = 8  -- Rising upward
local FRAME_FALLING = 9  -- Falling downward

-- Physics Profiles
local GROUND_Y     = 78   -- Screen Y coordinate for the floor
local GRAVITY      = 0.15 -- Constant downward pull
local JUMP_FORCE   = -2.8 -- Upward velocity blast (Impulse)

-- Live Entities Coordinates
local f  = FRAME_GROUND
local py = GROUND_Y
local dy = 0

function sprite.tick()
    cls(0)
    sbank(0)

    -- 1. Input Handler (Only trigger jump if player is sitting on the floor)
    if py >= GROUND_Y and btn(4) then
        dy = JUMP_FORCE
    end

    -- 2. Apply Physical Kinematics
    py += dy
    dy += GRAVITY

    -- 3. State Machine & Floor Bounds Constraint (Fake Collision)
    if py >= GROUND_Y then
        -- Snap exactly to the floor and halt downward acceleration
        py = GROUND_Y
        dy = 0
        f = FRAME_GROUND
    else
        -- Player is mid-air! Pick the frame based purely on vertical direction
        if dy < 0 then
            f = FRAME_JUMPING -- Moving Up
        else
            f = FRAME_FALLING -- Moving Down
        end
    end

    -- 4. Render Pipeline
    -- Using flr() prevents sub-pixel jitter artifacts on retro draw grids
    spr(f, 60, flr(py)) 

    -- Debug Display Interface
    text(string.format("dy: %.2f", dy), 4, 4, 13)
    text("press A to jump", 4, 87, 13)
end
