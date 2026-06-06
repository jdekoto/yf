local ADDR_MAP   = 0x2A500  -- Updated base memory pointer to map space 
local MAP_WIDTH  = 256
local MAP_HEIGHT = 192

function tile(x, y, tile_num)
    if x < 0 or x >= MAP_WIDTH or y < 0 or y >= MAP_HEIGHT then
        if tile_num == nil then return 0 end
        return
    end

    local target_address = ADDR_MAP + (y * MAP_WIDTH + x)

    if tile_num == nil then
        return peek(target_address)
    else
        poke(target_address, tile_num & 0xFF)
    end
end

function mget(x, y)
    return tile(x, y)
end

function mset(x, y, tile_num)
    tile(x, y, tile_num)
end

function map(start_tile_x, start_tile_y, screen_x, screen_y, tiles_w, tiles_h)
    -- Fallback: If no width/height are specified, default to what fits on the screen
    -- 128 / 8 = 16 tiles wide, 96 / 8 = 12 tiles high
    tiles_w = tiles_w or 16
    tiles_h = tiles_h or 12
    
    for ty = 0, tiles_h - 1 do
        local map_y = start_tile_y + ty
        for tx = 0, tiles_w - 1 do
            local map_x = start_tile_x + tx
            
            -- Grab the sprite ID assigned to this grid coordinate
            local sprite_id = mget(map_x, map_y)
            
            -- Only draw if it's a visible, non-zero sprite (treating 0 as transparent/empty sky)
            if sprite_id > 0 then
                -- Calculate the absolute world coordinates where this tile should be drawn
                local draw_x = screen_x + (tx * 8)
                local draw_y = screen_y + (ty * 8)
                
                -- Pass it to your existing spr() renderer!
                spr(sprite_id, draw_x, draw_y)
            end
        end
    end
end
