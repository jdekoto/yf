local REG_AUDIO = 0x03050

function play_pcm(channel, address, length, loop, volume)
    local offset = REG_AUDIO + (channel * 10)
    
    -- Extract 24-bit address bytes
    local a0 = (address >> 16) & 0xFF
    local a1 = (address >> 8) & 0xFF
    local a2 = address & 0xFF
    
    -- Extract 24-bit length bytes
    local l0 = (length >> 16) & 0xFF
    local l1 = (length >> 8) & 0xFF
    local l2 = length & 0xFF
    
    -- Write configurations to RAM
    poke(offset + 2, loop and 1 or 0)
    poke(offset + 3, a0)
    poke(offset + 4, a1)
    poke(offset + 5, a2)
    poke(offset + 6, l0)
    poke(offset + 7, l1)
    poke(offset + 8, l2)
    poke(offset + 9, volume)
    
    -- Fire Triggers!
    poke(offset + 1, 1) -- Tell C to clear the playhead position
    poke(offset + 0, 1) -- Turn status to 1 (Play!)
end

-- DEDICATED MANUAL RESET FUNCTION (Pure Poke!)
function reset_channel_playhead(channel)
    local offset = REG_AUDIO + (channel * 10)
    poke(offset + 1, 1) -- Just drop a 1 into the trigger byte register slot!
end

function stop_pcm(channel)
    poke(REG_AUDIO + (channel * 10) + 0, 0)
end
