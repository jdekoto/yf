-- Hardware Addresses (16-bit layout)
local ADDR_INP_CURRENT_LOW   = 0x06040
local ADDR_INP_CURRENT_HIGH  = 0x06041
local ADDR_INP_PREVIOUS_LOW  = 0x06042
local ADDR_INP_PREVIOUS_HIGH = 0x06043

-- Maps to the bit offsets
BTN_LEFT  = 0  -- (1 << 0)
BTN_RIGHT = 1  -- (1 << 1)
BTN_UP    = 2  -- (1 << 2)
BTN_DOWN  = 3  -- (1 << 3)
BTN_A     = 4  -- (1 << 4)
BTN_B     = 5  -- (1 << 5)
BTN_C     = 6  -- (1 << 6)
BTN_D     = 7  -- (1 << 7)
BTN_ENTER = 8  -- (1 << 8)

-- Reconstructs the 16-bit word from memory maps dynamically
local function get_current_mask()
    return peek(ADDR_INP_CURRENT_LOW) | (peek(ADDR_INP_CURRENT_HIGH) << 8)
end

local function get_previous_mask()
    return peek(ADDR_INP_PREVIOUS_LOW) | (peek(ADDR_INP_PREVIOUS_HIGH) << 8)
end

--- Checks if a button is currently held down
function btn(id)
    local current = get_current_mask()
    return (current & (1 << id)) ~= 0
end

--- Checks if a button was pressed THIS frame, but not last frame
function btnp(id)
    local current  = get_current_mask()
    local previous = get_previous_mask()
    
    local is_down_now = (current & (1 << id)) ~= 0
    local was_up_then = (previous & (1 << id)) == 0
    
    return is_down_now and was_up_then
end
