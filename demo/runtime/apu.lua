local ADDR_AUDIO = 0x03050
local function CH_STATUS(ch)  return ADDR_AUDIO + (ch * 10) + 0 end
local function CH_TRIGGER(ch) return ADDR_AUDIO + (ch * 10) + 1 end
local function CH_LOOP(ch)    return ADDR_AUDIO + (ch * 10) + 2 end
local function CH_ADDR_0(ch)  return ADDR_AUDIO + (ch * 10) + 3 end
local function CH_ADDR_1(ch)  return ADDR_AUDIO + (ch * 10) + 4 end
local function CH_ADDR_2(ch)  return ADDR_AUDIO + (ch * 10) + 5 end
local function CH_LEN_0(ch)   return ADDR_AUDIO + (ch * 10) + 6 end
local function CH_LEN_1(ch)   return ADDR_AUDIO + (ch * 10) + 7 end
local function CH_LEN_2(ch)   return ADDR_AUDIO + (ch * 10) + 8 end
local function CH_VOLUME(ch)  return ADDR_AUDIO + (ch * 10) + 9 end



--[[
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
]]--
-- runtime/audio.lua

-- Helper functions to unpack little-endian integers from a binary string
local function unpack_u16(str, offset)
    local b1, b2 = string.byte(str, offset, offset + 1)
    return b1 + (b2 << 8)
end

local function unpack_u32(str, offset)
    local b1, b2, b3, b4 = string.byte(str, offset, offset + 3)
    return b1 + (b2 << 8) + (b3 << 16) + (b4 << 24)
end

-- runtime/audio.lua

function sfx(filename, volume, channel)
    channel = channel or 2
    volume = math.floor((volume or 1.0) * 255)
    if volume > 255 then volume = 255 end

    local file = io.open(filename, "rb")
    if not file then return false end

    -- Read file and parse data size
    local current = file:seek()
    local file_size = file:seek("end")
    file:seek("set", current)
    local header = file:read(44)
    local data_size = file_size - 44
    local pcm_data = file:read(data_size)
    file:close()

    -- Calculate destination relative to the new ADDR_SNDBUF location
    local addr_sndbuf = 0xE0000
    local audio_ram_dest = addr_sndbuf + (channel * 0x8000) -- 32KB shifts

    -- Safety check at 32KB
    if data_size > 32768 then data_size = 32768 end 

    -- Poke the raw payload bytes into the high memory bank
    for i = 1, data_size do
        local u8_sample = string.byte(pcm_data, i)
        poke(audio_ram_dest + (i - 1), u8_sample)
    end

    -- Clear trailing buffer space to silent center (128)
    for i = data_size, 32768 do
        poke(audio_ram_dest + i, 128)
    end

    -- Register packing math
    local addr_high = (audio_ram_dest >> 16) & 0xFF
    local addr_mid  = (audio_ram_dest >> 8) & 0xFF
    local addr_low  = audio_ram_dest & 0xFF

    local len_high = math.floor(data_size / 65536) & 0xFF
    local len_mid  = math.floor(data_size / 256) & 0xFF
    local len_low  = data_size & 0xFF

    -- Fire away!
    poke(CH_VOLUME(channel), volume)
    poke(CH_ADDR_0(channel), addr_high)
    poke(CH_ADDR_1(channel), addr_mid)
    poke(CH_ADDR_2(channel), addr_low)
    poke(CH_LEN_0(channel),  len_high)
    poke(CH_LEN_1(channel),  len_mid)
    poke(CH_LEN_2(channel),  len_low)
    poke(CH_LOOP(channel),   0)
    poke(CH_STATUS(channel), 1)
    poke(CH_TRIGGER(channel), 1)

    return true
end

-- runtime/music.lua

-- Define the exact hex addresses matching the C side definitions
local IO_TRACKER_ENABLED = 0x03092 -- Swap this hex number with your exact calculation if needed!
local IO_TRACKER_VOLUME  = 0x03093

Tracker = {
    is_playing = false,
    volume = 1.0,
    fade_target = 1.0,
    fade_speed = 0.0,
    fade_callback = nil
}

function Tracker.load(filename)
    feed_tracker(filename) -- Call our clean native C asset loading function wrapper
    Tracker.is_playing = false
    Tracker.volume = 1.0
    
    poke(IO_TRACKER_ENABLED, 0)
    poke(IO_TRACKER_VOLUME, 255) 
end

function Tracker.play()
    Tracker.is_playing = true
    poke(IO_TRACKER_ENABLED, 1) -- Set hardware flag high! C takes over completely!
end

function Tracker.pause()
    Tracker.is_playing = false
    poke(IO_TRACKER_ENABLED, 0) -- Drop flag low to pause stream immediately
end

function Tracker.fade(target_volume, duration_frames, on_complete)
    Tracker.fade_target = target_volume
    Tracker.fade_callback = on_complete
    
    if duration_frames <= 0 then
        Tracker.volume = target_volume
        poke(IO_TRACKER_VOLUME, math.floor(target_volume * 255))
    else
        Tracker.fade_speed = (target_volume - Tracker.volume) / duration_frames
    end
end

function Tracker.update()
    -- Process the creative fading loop updates
    if Tracker.volume ~= Tracker.fade_target then
        Tracker.volume = Tracker.volume + Tracker.fade_speed
        
        if (Tracker.fade_speed > 0 and Tracker.volume >= Tracker.fade_target) or
           (Tracker.fade_speed < 0 and Tracker.volume <= Tracker.fade_target) then
            Tracker.volume = Tracker.fade_target
            Tracker.fade_speed = 0
            
            if Tracker.fade_callback then 
                Tracker.fade_callback() 
                Tracker.fade_callback = nil
            end
        end
        
        local raw_u8_vol = math.floor(Tracker.volume * 255)
        if raw_u8_vol > 255 then raw_u8_vol = 255 end
        if raw_u8_vol < 0   then raw_u8_vol = 0 end
        poke(IO_TRACKER_VOLUME, raw_u8_vol)
    end
end
