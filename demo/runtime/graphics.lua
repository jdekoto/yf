-- runtime/graphics.lua
local ADDR_FB   = 0x00000 --
local ADDR_FONT = 0x06200 --
local FB_WID    = 128     --
local FB_HEI    = 96      --
local ADDR_BANK_SWITCH = 0x06044 --
local PALETTE = {}

-- Persistent tracking tables for sprite bank metadata
local BANK_ADDRESSES = {
    [0] = 0x06500,        	  -- Bank 0 baseline RAM address
    [1] = 0x08500, 		  	  -- Bank 1 baseline RAM address (shifted past a full 64*64 block)
}
local BANK_WIDTHS  = { [0] = 64, [1] = 64 } -- Defaults
local BANK_HEIGHTS = { [0] = 64, [1] = 64 } -- Defaults

-- Global Camera Tracking States (Defaults to 0, 0)
local CAM_X = 0
local CAM_Y = 0

-- Global Clipping Rect tracking (Defaults to full-screen bounds)
local CLIP = false
local CLIP_X0 = 0
local CLIP_Y0 = 0
local CLIP_X1 = FB_WID - 1
local CLIP_Y1 = FB_HEI - 1

-- Helper: Packs 0-255 RGB channels into a single 16-bit RGB565 integer
function rgb(r, g, b)
    local r5 = math.floor((r * 31) / 255)
    local g6 = math.floor((g * 63) / 255)
    local b5 = math.floor((b * 31) / 255)
    return (r5 * 2048) + (g6 * 32) + b5
end

local PALETTE = {
  [0] = rgb(23, 25, 27), 	-- asphalt
  [1] = rgb(40, 35, 123), 	-- ocean
  [2] = rgb(50, 89, 226), 	-- afternoon
  [3] = rgb(51, 165, 255), 	-- neon
  [4] = rgb(10, 75, 77), 	-- rainforest
  [5] = rgb(114, 203, 37), 	-- bamboo
  [6] = rgb(255, 196, 56), 	-- solar
  [7] = rgb(240, 108, 0), 	-- tangerine
  [8] = rgb(209, 40, 65), 	-- strawberry
  [9] = rgb(87, 20, 46), 	-- cherry
  [10] = rgb(151, 63, 63), 	-- soil
  [11] = rgb(241, 194, 132), 	-- caucasian
  [12] = rgb(229, 93, 172), 	-- bubblegum
  [13] = rgb(241, 240, 238), 	-- white
  [14] = rgb(150, 165, 171), 	-- cobblestone
  [15] = rgb(88, 108, 121), 	-- concrete
}

function cls(color)
    if PALETTE[color] then
        color = PALETTE[color]
    end

    local low_byte  = color & 0xFF
    local high_byte = (color >> 8) & 0xFF
   
    for addr = ADDR_FB, ADDR_FB + 24575, 2 do
        poke(addr,     low_byte)
        poke(addr + 1, high_byte)
    end
end

function pset(x, y, color)
	-- Apply the global camera displacement offset!
    x = x - CAM_X -- this is basically cheap but idc anymore
    y = y - CAM_Y
    
    -- 2. Hardware Clipping Check
    -- If a custom clip window is active, drop pixels outside it!
    if CLIP then
        if x < CLIP_X0 or x > CLIP_X1 or y < CLIP_Y0 or y > CLIP_Y1 then 
            return 
        end
    end
    
    if x < 0 or x >= FB_WID or y < 0 or y >= FB_HEI then return end
    
    
 
    if PALETTE[color] then
        color = PALETTE[color]
    end
 
    local pixel_addr = ADDR_FB + ((y * FB_WID + x) * 2)
    
    local low_byte  = color & 0xFF
    local high_byte = (color >> 8) & 0xFF
    
    poke(pixel_addr,     low_byte)
    poke(pixel_addr + 1, high_byte)
end

function pget(x, y)
    if x >= 0 and x < 128 and y >= 0 and y < 96 then
        local addr = 0x00000 + (y * 128) + x
        peek(addr)
    end
end

function rectfill(x0, y0, x1, y1, color)
    -- Standardize bounds sorting (handles drawing backwards cleanly)
    local start_x = math.min(x0, x1)
    local end_x = math.max(x0, x1)
    local start_y = math.min(y0, y1)
    local end_y = math.max(y0, y1)
    
    -- Hard clipping boundaries protection
    if start_x < 0 then start_x = 0 end
    if end_x > 127 then end_x = 127 end
    if start_y < 0 then start_y = 0 end
    if end_y > 95 then end_y = 95 end

    -- Optimize by running pset loop directly 
    for y = start_y, end_y do
        for x = start_x, end_x do
            pset(x, y, color)
        end
    end
end

function rect(x0, y0, x1, y1, color)
    -- Draw a wireframe rectangle using four straight lines or fills
    rectfill(x0, y0, x1, y0, color) -- Top edge
    rectfill(x0, y1, x1, y1, color) -- Bottom edge
    rectfill(x0, y0, x0, y1, color) -- Left edge
    rectfill(x1, y0, x1, y1, color) -- Right edge
end

function line(x0, y0, x1, y1, color)
    x0, y0, x1, y1 = math.floor(x0), math.floor(y0), math.floor(x1), math.floor(y1)
    local dx = math.abs(x1 - x0)
    local dy = math.abs(y1 - y0)
    local sx = x0 < x1 and 1 or -1
    local sy = y0 < y1 and 1 or -1
    local err = dx - dy

    while true do
        pset(x0, y0, color)
        
        if x0 == x1 and y0 == y1 then break end
        
        local e2 = 2 * err
        if e2 > -dy then
            err = err - dy
            x0 = x0 + sx
        end
        if e2 < dx then
            err = err + dx
            y0 = y0 + sy
        end
    end
end

function circfill(cx, cy, r, color)
    cx, cy, r = math.floor(cx), math.floor(cy), math.floor(r)
    if r < 0 then return end
    
    local x = r
    local y = 0
    local err = 1 - r

    while x >= y do
        -- Draw horizontal bars across the circle quadrants to fill it cleanly
        line(cx - x, cy + y, cx + x, cy + y, color)
        line(cx - x, cy - y, cx + x, cy - y, color)
        line(cx - y, cy + x, cx + y, cy + x, color)
        line(cx - y, cy - x, cx + y, cy - x, color)

        y = y + 1
        if err < 0 then
            err = err + 2 * y + 1
        else
            x = x - 1
            err = err + 2 * (y - x) + 1
        end
    end
end

function camera(x, y)
    -- If no parameters are passed, reset the camera back to origin (0, 0)
    CAM_X = math.floor(x or 0)
    CAM_Y = math.floor(y or 0)
end

function clip(x, y, w, h)
    if not x or not y or not w or not h then
        -- Clear the clip: turn it off and reset to screen size
        CLIP_ENABLED = false
        CLIP_X0 = 0
        CLIP_Y0 = 0
        CLIP_X1 = FB_WID - 1
        CLIP_Y1 = FB_HEI - 1
    else
        -- Engage the clip bounding region
        CLIP_ENABLED = true
        CLIP_X0 = math.floor(x)
        CLIP_Y0 = math.floor(y)
        CLIP_X1 = math.floor(x + w - 1)
        CLIP_Y1 = math.floor(y + h - 1)
    end
end

-- texture/sprite handling
local function unpack_uint32(str, offset)
    local b1, b2, b3, b4 = string.byte(str, offset, offset + 3)
    return b1 + (b2 << 8) + (b3 << 16) + (b4 << 24)
end

local function unpack_int32(str, offset)
    local val = unpack_uint32(str, offset)
    if val >= 0x80000000 then return val - 0x100000000 end
    return val
end

function sbank(bank_id)
    poke(ADDR_BANK_SWITCH, bank_id or 0)
end

-- Upgraded Asset Loader supporting dynamic targets: sprsht("sprites.bmp", 0)
function sprsht(filename, bank_id)
    bank_id = bank_id or 0
    local dest_ram_address = BANK_ADDRESSES[bank_id]
    
    if not dest_ram_address then
        print("Error: Invalid Bank ID selection: " .. tostring(bank_id))
        return false
    end

    local file = io.open(filename, "rb")
    if not file then return false end

    local header = file:read(54)
    if not header or string.sub(header, 1, 2) ~= "BM" then
        file:close()
        return false
    end

    local pixel_data_offset = unpack_uint32(header, 11)
    local width              = unpack_int32(header, 19)
    local height             = unpack_int32(header, 23)
    local bits_per_pixel     = string.byte(header, 29) + (string.byte(header, 30) << 8)

    file:seek("set", pixel_data_offset)

    local abs_height = math.abs(height)
    local is_bottom_up = (height > 0)

    if bits_per_pixel == 24 then
        local row_size = math.floor(((24 * width) + 31) / 32) * 4
        local pixel_bytes = file:read(row_size * abs_height)
        file:close()

		if width > 64 or abs_height > 64 then
        		print(string.format("CRITICAL ERROR: '%s' is %dx%d. Max spritesheet dimensions allowed: 64x64!", 
            		filename, width, abs_height))
        		return false
    		end

        -- Dynamically log the true sheet proportions so our spr() renderer can adapt!
        BANK_WIDTHS[bank_id]  = width
        BANK_HEIGHTS[bank_id] = abs_height

        for y = 0, abs_height - 1 do
            local bmp_y = is_bottom_up and (abs_height - 1 - y) or y
            local row_start = (bmp_y * row_size) + 1

            for x = 0, width - 1 do
                local byte_pos = row_start + (x * 3)
                local b = string.byte(pixel_bytes, byte_pos)
                local g = string.byte(pixel_bytes, byte_pos + 1)
                local r = string.byte(pixel_bytes, byte_pos + 2)

                local color16 = rgb(r, g, b)

                -- Calculate absolute destination index sequentially
                local target_ram_addr = dest_ram_address + ((y * width + x) * 2)
                poke(target_ram_addr,     color16 & 0xFF)
                poke(target_ram_addr + 1, (color16 >> 8) & 0xFF)
            end
        end
    else
        print("Error: Save asset sprites as 24-bit BMP.")
        file:close()
        return false
    end

    print("Loaded " .. filename .. " (" .. width .. "x" .. abs_height .. ") into Sprite Bank " .. bank_id)
    return true
end

-- Upgraded 16-Bit Sprite Renderer that scales beautifully to fit 64x64 grids
function spr(id, screen_x, screen_y, flip_x, flip_y)
    local current_bank = peek(ADDR_BANK_SWITCH)
    
    local sprite_sheet_base  = BANK_ADDRESSES[current_bank] or 0x06500
    local sprite_sheet_width = BANK_WIDTHS[current_bank] or 64

    -- Calculate how many 8x8 sprite columns fit across this specific sheet width
    local total_cols = math.floor(sprite_sheet_width / 8)

    local spr_x = (id % total_cols) * 8
    local spr_y = math.floor(id / total_cols) * 8

    for py = 0, 7 do
        for px = 0, 7 do
            local source_x = spr_x + (flip_x and (7 - px) or px)
            local source_y = spr_y + (flip_y and (7 - py) or py)

            local source_addr = sprite_sheet_base + ((source_y * sprite_sheet_width + source_x) * 2)
            local low = peek(source_addr)
            local high = peek(source_addr + 1)
            local color16 = low | (high << 8)

            -- Treat pure absolute black (0x0000) as transparent!
            if color16 ~= 0x0000 then
                pset(screen_x + px, screen_y + py, color16)
            end
        end
    end
end

--text/font rendering
-- TODO: update character map for special characters
local ASCII_TO_FONT_INDEX = {}
local sequential_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ !__0123456789.:(){}-+/*,=\"'_[]____?<>@#$%^__~"

for i = 1, #sequential_chars do
    local b = sequential_chars:byte(i)
    ASCII_TO_FONT_INDEX[b] = i - 1
end

ASCII_TO_FONT_INDEX[string.byte("_")] = 43  
ASCII_TO_FONT_INDEX[string.byte("[")] = 44  
ASCII_TO_FONT_INDEX[string.byte("]")] = 45  
ASCII_TO_FONT_INDEX[string.byte("{")] = 47  
ASCII_TO_FONT_INDEX[string.byte("}")] = 48  
ASCII_TO_FONT_INDEX[string.byte("^")] = 49  
ASCII_TO_FONT_INDEX[string.byte("?")] = 50  
ASCII_TO_FONT_INDEX[string.byte("<")] = 51  
ASCII_TO_FONT_INDEX[string.byte(">")] = 52  
ASCII_TO_FONT_INDEX[string.byte("@")] = 53  
ASCII_TO_FONT_INDEX[string.byte("#")] = 54  
ASCII_TO_FONT_INDEX[string.byte("$")] = 55  
ASCII_TO_FONT_INDEX[string.byte("%")] = 56  
ASCII_TO_FONT_INDEX[string.byte("&")] = 57  
ASCII_TO_FONT_INDEX[string.byte("~")] = 58  

local function get_char_width(font_char_addr)
    local max_col = 0
    for row = 0, 4 do
        local row_byte = peek(font_char_addr + row)
        for col = 0, 4 do
            local bit_mask = 0x80 >> col
            if (row_byte & bit_mask) ~= 0 then
                if col > max_col then max_col = col end
            end
        end
    end
    if max_col == 0 then return 2 end
    return max_col + 1
end

-- 16-Bit Variant Text Printer
function text(str, x, y, color)
    if PALETTE[color] then
        color = PALETTE[color]
    end
    -- Default to pure white color if no value is explicitly declared
    color = color or rgb(255, 255, 255)
    local start_x = x

    str = string.upper(str)

    for i = 1, #str do
        local byte_code = str:byte(i)
        
        if byte_code == 10 then 
            x = start_x
            y = y + 6 
        else
            local idx = ASCII_TO_FONT_INDEX[byte_code]
            if idx then
                local font_char_addr = ADDR_FONT + (idx * 6)
                local char_width = get_char_width(font_char_addr)
                
                for row = 0, 4 do
                    local row_byte = peek(font_char_addr + row)
                    for col = 0, char_width - 1 do
                        local bit_mask = 0x80 >> col
                        
                        if (row_byte & bit_mask) ~= 0 then
                            -- Draw using our newly updated 16-bit pset!
                            pset(x + col, y + row, color)
                        end
                    end
                end
                x = x + char_width + 1
            else
                x = x + 4
            end
        end
    end
end
