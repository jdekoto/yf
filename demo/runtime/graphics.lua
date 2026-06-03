local ADDR_FB   = 0x00000 --
local ADDR_FONT = 0x03200 --
local FB_WID    = 128     --
local FB_HEI    = 96      --

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
