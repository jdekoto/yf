local ADDR_FB   = 0x00000 --
local ADDR_FONT = 0x06200 --
local FB_WID    = 128     --
local FB_HEI    = 96      --
local ADDR_BANK_SWITCH = 0x03044 --
local PALETTE = {}

-- runtime/graphics.lua

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

local function unpack_uint32(str, offset)
    local b1, b2, b3, b4 = string.byte(str, offset, offset + 3)
    return b1 + (b2 << 8) + (b3 << 16) + (b4 << 24)
end

local function unpack_int32(str, offset)
    local val = unpack_uint32(str, offset)
    if val >= 0x80000000 then return val - 0x100000000 end
    return val
end

-- NOTE: To support true 16-bit color rendering, your 'sprites.bmp' file should ideally 
-- be saved from your image editor directly as an uncompressed 24-bit RGB or 16-bit BMP!
function sprsht(filename, dest_ram_address)
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
        -- 24-bit True Color BMP processing (Easiest format to export from modern software)
        local row_size = math.floor(((24 * width) + 31) / 32) * 4
        local pixel_bytes = file:read(row_size * abs_height)
        file:close()

        for y = 0, abs_height - 1 do
            local bmp_y = is_bottom_up and (abs_height - 1 - y) or y
            local row_start = (bmp_y * row_size) + 1

            for x = 0, width - 1 do
                local byte_pos = row_start + (x * 3)
                local b = string.byte(pixel_bytes, byte_pos)
                local g = string.byte(pixel_bytes, byte_pos + 1)
                local r = string.byte(pixel_bytes, byte_pos + 2)

                -- Convert 24-bit file color into your engine's 16-bit RGB565 short
                local color16 = rgb(r, g, b)

                -- Store the 2 color bytes consecutively inside Cartridge Asset RAM
                local target_ram_addr = dest_ram_address + ((y * width + x) * 2)
                poke(target_ram_addr,     color16 & 0xFF)
                poke(target_ram_addr + 1, (color16 >> 8) & 0xFF)
            end
        end
    else
        print("Error: For direct 16-bit high color modes, save asset sprites as 24-bit BMP.")
        file:close()
        return false
    end

    print("Loaded " .. filename .. " (" .. width .. "x" .. abs_height .. ") into Cartridge Asset RAM!")
    return true
end

function sbank(bank_id)
    poke(ADDR_BANK_SWITCH, bank_id or 0)
end

-- 16-Bit Sprite Renderer
function spr(id, screen_x, screen_y, flip_x, flip_y)
    local current_bank = peek(ADDR_BANK_SWITCH)
    
    -- In 16-bit mode, our spritesheet base needs double the storage footprint
    local sprite_sheet_base = 0x06500 -- Safely past VRAM and standard hardware components
    if current_bank == 1 then
        sprite_sheet_base = sprite_sheet_base + 32768 -- Offsets by 32KB per bank
    end

    local sprite_sheet_width = 128
    local spr_x = (id % 16) * 8
    local spr_y = math.floor(id / 16) * 8

    for py = 0, 7 do
        for px = 0, 7 do
            local source_x = spr_x + (flip_x and (7 - px) or px)
            local source_y = spr_y + (flip_y and (7 - py) or py)

            -- Read the 16-bit color stored inside Asset RAM for this pixel location
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

-- ─── SYSTEM TEXT SUBSYSTEM ──────────────────────────────────────────────────

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

