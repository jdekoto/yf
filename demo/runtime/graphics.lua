local ADDR_FB   = 0x00000 --
local ADDR_FONT = 0x03200 --
local FB_WID    = 128     --
local FB_HEI    = 96      --
local ADDR_BANK_SWITCH = 0x03044 --

function cls(color)
    memcpy(0x00000, color, 12288) 
end

function pset(x, y, color)
    if x >= 0 and x < 128 and y >= 0 and y < 96 then
        local addr = 0x00000 + (y * 128) + x
        poke(addr, color & 0x0F)
    end
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

    for y = start_y, end_y do
        local row_offset = y * 128
        for x = start_x, end_x do
            poke(row_offset + x, color)
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

-- Inside runtime/render.lua

-- Helper function to unpack a 4-byte little-endian integer from a binary string
local function unpack_uint32(str, offset)
    local b1, b2, b3, b4 = string.byte(str, offset, offset + 3)
    return b1 + (b2 << 8) + (b3 << 16) + (b4 << 24)
end

-- Helper function to unpack a 4-byte signed integer (handles negative heights)
local function unpack_int32(str, offset)
    local val = unpack_uint32(str, offset)
    if val >= 0x80000000 then
        return val - 0x100000000
    end
    return val
end

--- Loads an indexed 8-bit or 4-bit BMP file completely via Lua and pokes it into RAM
-- @param filename The path to the .bmp file (e.g., "sprites0.bmp")
-- @param dest_ram_address The target VRAM memory offset (e.g., 0x04000)
function sprsht(filename, dest_ram_address)
    local file = io.open(filename, "rb")
    if not file then
        print("Error: Could not open file " .. filename)
        return false
    end

    -- Read the entire file header block (first 54 bytes cover BMP metadata)
    local header = file:read(54)
    if not header or string.sub(header, 1, 2) ~= "BM" then
        print("Error: " .. filename .. " is not a valid BMP file.")
        file:close()
        return false
    end

    -- Extract file pointers using our unpackers
    local pixel_data_offset = unpack_uint32(header, 11) -- Byte 10: Pixel data start address
    local width              = unpack_int32(header, 19)  -- Byte 18: Image Width
    local height             = unpack_int32(header, 23)  -- Byte 22: Image Height
    local bits_per_pixel     = string.byte(header, 29) + (string.byte(header, 30) << 8)

    if bits_per_pixel ~= 8 and bits_per_pixel ~= 4 then
        print("Error: Console only supports 8-bit or 4-bit indexed BMP formats.")
        file:close()
        return false
    end

    -- Jump directly to the pixel data offset block inside the file
    file:seek("set", pixel_data_offset)

    -- BMP rows are padded to multiples of 4 bytes. Let's calculate the true row size:
    local row_size = math.floor(((bits_per_pixel * width) + 31) / 32) * 4
    
    -- Read all raw pixel rows into memory
    local pixel_bytes = file:read(row_size * math.abs(height))
    file:close()

    -- Process rows. Remember: BMP maps pixels from bottom-to-top by default (positive height)
    local is_bottom_up = (height > 0)
    local abs_height = math.abs(height)

    for y = 0, abs_height - 1 do
        local bmp_y = is_bottom_up and (abs_height - 1 - y) or y
        local row_start = (bmp_y * row_size) + 1

        for x = 0, width - 1 do
            local color_index = 0

            if bits_per_pixel == 8 then
                -- 8-bit: Each byte is exactly one pixel color index
                local byte_pos = row_start + x
                color_index = string.byte(pixel_bytes, byte_pos) & 0x0F -- Clamp to 16-color range
            elseif bits_per_pixel == 4 then
                -- 4-bit: 1 byte contains 2 pixels packed together (High nibble, Low nibble)
                local byte_pos = row_start + math.floor(x / 2)
                local raw_byte = string.byte(pixel_bytes, byte_pos)
                if x % 2 == 0 then
                    color_index = (raw_byte >> 4) & 0x0F -- Left pixel
                else
                    color_index = raw_byte & 0x0F        -- Right pixel
                end
            end

            -- Poke the resulting color index directly into your mapped Sprite RAM!
            local target_ram_addr = dest_ram_address + (y * width) + x
            poke(target_ram_addr, color_index)
        end
    end

    print("Successfully loaded " .. filename .. " (" .. width .. "x" .. abs_height .. ") via Lua!")
    return true
end

--- Selects which sprite sheet bank spr() will read from
-- @param bank_id (0 for sprites0.bmp, 1 for sprites1.bmp)
function sbank(bank_id)
    poke(ADDR_BANK_SWITCH, bank_id or 0)
end

--- Draws an 8x8 sprite from the active sheet bank directly into VRAM
function spr(id, screen_x, screen_y, flip_x, flip_y)
    -- 1. Determine which 16KB memory block to read from based on our bank switch register
    local current_bank = peek(ADDR_BANK_SWITCH)
    local sprite_sheet_base = 0x04000
    if current_bank == 1 then
        sprite_sheet_base = 0x08000
    end

    -- 2. Calculate where this sprite ID lives on a 128x128 grid (16 sprites per row)
    local sprite_sheet_width = 128
    local spr_x = (id % 16) * 8
    local spr_y = math.floor(id / 16) * 8

    -- 3. Draw the 8x8 pixel block row by row
    for py = 0, 7 do
        for px = 0, 7 do
            -- Handle horizontal and vertical flipping math
            local source_x = spr_x + (flip_x and (7 - px) or px)
            local source_y = spr_y + (flip_y and (7 - py) or py)

            -- Compute the exact RAM address of the source pixel and read it
            local source_addr = sprite_sheet_base + (source_y * sprite_sheet_width) + source_x
            local color = peek(source_addr)

            -- Color 0 is traditionally transparent! Skip drawing if it matches
            if color ~= 0 then
                local dest_x = screen_x + px
                local dest_y = screen_y + py

                -- Bounds safety check: Only poke if the pixel falls inside your screen boundaries
                if dest_x >= 0 and dest_x < FB_WID and dest_y >= 0 and dest_y < FB_HEI then
                    local dest_addr = ADDR_FB + (dest_y * FB_WID) + dest_x
                    poke(dest_addr, color)
                end
            end
        end
    end
end

-- An optimization lookup table mapping ASCII character byte codes directly to your sheet indices
local ASCII_TO_FONT_INDEX = {}

-- 1. Initialize standard ASCII mappings sequentially based on your font.png order
local sequential_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ !__0123456789.:(){}-+/*,=\"'_[]____?<>@#$%^__~"

for i = 1, #sequential_chars do
    local b = sequential_chars:byte(i)
    ASCII_TO_FONT_INDEX[b] = i - 1 -- 0-based index for memory math
end

-- 2. Map your special custom graphics to standard keyboard characters for easy typing!
-- Looking at the symbols remaining on your font.png strip:
ASCII_TO_FONT_INDEX[string.byte("_")] = 43  -- Underscore
ASCII_TO_FONT_INDEX[string.byte("[")] = 44  -- Left Bracket
ASCII_TO_FONT_INDEX[string.byte("]")] = 45  -- Right Bracket

-- Custom retro symbols at the end of your sheet:
-- ASCII_TO_FONT_INDEX[string.byte("C")] = 46  -- 'C' for Cup / Mug icon
ASCII_TO_FONT_INDEX[string.byte("{")] = 47  -- '{' for Heart icon
ASCII_TO_FONT_INDEX[string.byte("}")] = 48  -- '}' for Diamond icon
ASCII_TO_FONT_INDEX[string.byte("^")] = 49  -- '^' for Up Arrow icon
ASCII_TO_FONT_INDEX[string.byte("?")] = 50  -- Question Mark
ASCII_TO_FONT_INDEX[string.byte("<")] = 51  -- Less Than
ASCII_TO_FONT_INDEX[string.byte(">")] = 52  -- Greater Than
ASCII_TO_FONT_INDEX[string.byte("@")] = 53  -- At symbol
ASCII_TO_FONT_INDEX[string.byte("#")] = 54  -- Hash
ASCII_TO_FONT_INDEX[string.byte("$")] = 55  -- Dollar
ASCII_TO_FONT_INDEX[string.byte("%")] = 56  -- Percent
ASCII_TO_FONT_INDEX[string.byte("&")] = 57  -- Ampersand
ASCII_TO_FONT_INDEX[string.byte("~")] = 58  -- Tilde / Wave

-- Helper function to calculate the true visual width of a character in RAM
local function get_char_width(font_char_addr)
    local max_col = 0 -- Keep track of the rightmost column containing a pixel
    
    for row = 0, 4 do
        local row_byte = peek(font_char_addr + row)
        
        for col = 0, 4 do
            local bit_mask = 0x80 >> col
            if (row_byte & bit_mask) ~= 0 then
                if col > max_col then
                    max_col = col
                end
            end
        end
    end
    
    -- If max_col is 0, it means it's a completely empty space character.
    -- We give space characters a default width of 2 pixels.
    if max_col == 0 then
        return 2
    end
    
    -- True width is the index + 1 (e.g., if rightmost pixel is at col 2, width is 3)
    return max_col + 1
end

function text(str, x, y, color)
    color = (color or 13) & 0x0F 
    local start_x = x

    str = string.upper(str)

    for i = 1, #str do
        local byte_code = str:byte(i)
        
        if byte_code == 10 then -- Newline character (\n)
            x = start_x
            y = y + 6 
        else
            -- Check our O(1) fast lookup table for the font strip index
            local idx = ASCII_TO_FONT_INDEX[byte_code]
            
            if idx then
                local font_char_addr = ADDR_FONT + (idx * 6)
                
                -- DYNAMIC VARIABLE SPACING CHECK
                -- Calculate how wide this specific character actually is right now!
                local char_width = get_char_width(font_char_addr)
                
                -- Loop through the 5 rows vertically
                for row = 0, 4 do
                    local row_byte = peek(font_char_addr + row)
                    
                    -- Only loop up to the true width of the character!
                    for col = 0, char_width - 1 do
                        local bit_mask = 0x80 >> col
                        
                        if (row_byte & bit_mask) ~= 0 then
                            local pixel_x = x + col
                            local pixel_y = y + row
                            
                            if pixel_x >= 0 and pixel_x < FB_WID and pixel_y >= 0 and pixel_y < FB_HEI then
                                poke(ADDR_FB + (pixel_y * FB_WID) + pixel_x, color)
                            end
                        end
                    end
                end
                
                -- Advance x by the character's unique width + 1 pixel of kerning/padding!
                x = x + char_width + 1
            else
                -- If character isn't mapped, advance cursor by an empty space block
                x = x + 4
            end
        end
    end
end
