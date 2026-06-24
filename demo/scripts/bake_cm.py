import os
import sys
import struct

MAX_SOUND_SLOTS = 64
SOUNDBANK_SIZE = 64000  # Exactly 64KB system constraint
TOTAL_HEADER_SIZE = MAX_SOUND_SLOTS * 7  # 448 bytes

# Amiga ProTracker Period table matched to your 36-note engine pitch map
AMIGA_PERIODS = [
    # Octave 0
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    # Octave 1
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    # Octave 2
    214, 202, 190, 180, 170, 160, 151, 143, 134, 127, 120, 113
]

def period_to_note_index(period_val):
    if period_val == 0: return 0
    closest = min(AMIGA_PERIODS, key=lambda x: abs(x - period_val))
    if abs(closest - period_val) > 20: return 0
    return AMIGA_PERIODS.index(closest) + 1

def encode_brr_block(chunk):
    """Ported from bake_sndbnk.py: Compresses 16 signed 16-bit samples into 9 bytes"""
    if len(chunk) < 16:
        chunk = chunk + [0] * (16 - len(chunk))
        
    best_shift = 12
    for shift in range(13):
        valid = True
        for sample in chunk:
            step = (sample << 1) if shift == 0 else (sample >> (shift - 1))
            if not (-8 <= step <= 7):
                valid = False
                break
        if valid:
            best_shift = shift
            break

    header_byte = (best_shift << 4) | (0 << 2)
    block_bytes = bytearray([header_byte])
    
    for i in range(0, 16, 2):
        s0, s1 = chunk[i], chunk[i+1]
        step0 = (s0 << 1) if best_shift == 0 else (s0 >> (best_shift - 1))
        step1 = (s1 << 1) if best_shift == 0 else (s1 >> (best_shift - 1))
        
        step0 = max(-8, min(7, step0))
        step1 = max(-8, min(7, step1))
        
        packed_byte = ((step0 & 0x0F) << 4) | (step1 & 0x0F)
        block_bytes.append(packed_byte)
        
    return bytes(block_bytes)

def transpile_mod_to_brr(mod_path, output_cm_path, output_bin_path):
    print("--- Upgraded MOD Transpiler with Native BRR Packing ---")
    
    with open(mod_path, "rb") as f:
        mod_bytes = f.read()

    # 1. Parse 31 ProTracker instrument header structures
    sample_metadata = []
    offset = 20
    for i in range(31):
        name = mod_bytes[offset:offset+22].decode('ascii', errors='ignore').strip()
        sample_len = struct.unpack(">H", mod_bytes[offset+22:offset+24])[0] * 2
        finetune = mod_bytes[offset+24] & 0x0F
        volume = mod_bytes[offset+25]
        loop_start = struct.unpack(">H", mod_bytes[offset+26:offset+28])[0] * 2
        loop_len = struct.unpack(">H", mod_bytes[offset+28:offset+30])[0] * 2
        
        sample_metadata.append({
            "slot_idx": i,
            "len": sample_len,
            "loop_start": loop_start,
            "loop_len": loop_len,
            "has_loop": loop_len > 2
        })
        offset += 30

    song_length = mod_bytes[offset]
    offset += 2 # Skip restart marker
    
    order_list = list(mod_bytes[offset:offset+128])
    offset += 128
    
    magic = mod_bytes[offset:offset+4] # Skip "M.K." signature
    offset += 4
    
    num_patterns = max(order_list) + 1

    # 2. Translate pattern track data to your sequence file format
    translated_patterns = bytearray()
    for p in range(num_patterns):
        for row in range(64):
            for ch in range(4):
                b0, b1, b2, b3 = mod_bytes[offset:offset+4]
                offset += 4

                sample_idx = (b0 & 0xF0) | ((b2 & 0xF0) >> 4)
                period = ((b0 & 0x0F) << 8) | b1
                effect_cmd = b2 & 0x0F
                effect_param = b3

                engine_note = period_to_note_index(period)
                
                translated_patterns.append(engine_note)
                translated_patterns.append(sample_idx)
                translated_patterns.append(effect_cmd)
                translated_patterns.append(effect_param)

    # Output your core composition sequence module (.cm)
    with open(output_cm_path, "wb") as out_cm:
        out_cm.write(b"YFCM")
        out_cm.write(struct.pack("B", 125)) # Default BPM
        out_cm.write(struct.pack("B", 6))   # Ticks per row
        out_cm.write(struct.pack("<H", song_length))
        out_cm.write(bytes(order_list))
        out_cm.write(translated_patterns)

    # 3. Process, encode, and pack samples into the 64KB BRR Soundbank
    registry = [[0, 0, 0, 0] for _ in range(MAX_SOUND_SLOTS)]
    payload_accumulator = bytearray()
    current_write_offset = TOTAL_HEADER_SIZE

    for meta in sample_metadata:
        slot = meta["slot_idx"]
        s_len = meta["len"]

        if s_len == 0:
            continue

        # Extract raw signed 8-bit values from the file stream (-128 to 127)
        raw_pcm8 = mod_bytes[offset : offset + s_len]
        offset += s_len

        # Convert to signed 16-bit linear space to feed the BRR encoder
        pcm16_samples = []
        for b in raw_pcm8:
            # Cast unsigned Python byte read back into its true signed counterpart
            signed_b = b - 256 if b >= 128 else b
            pcm16_samples.append(signed_b << 8)

        # Iterate through chunks of 16 samples and compress into 9-byte BRR blocks
        brr_data = bytearray()
        for i in range(0, len(pcm16_samples), 16):
            chunk = pcm16_samples[i : i + 16]
            brr_data.extend(encode_brr_block(chunk))

        total_blocks = len(brr_data) // 9
        if total_blocks == 0:
            continue

        # Map Loop offsets from sample-domain to BRR block boundaries
        loop_block_offset = meta["loop_start"] // 16 if meta["has_loop"] else 0

        # Calculate engine flags: Bit 0 = Looping, Bit 1 = BRR Mode Active
        # Looping BRR = 3 (0x03), One-Shot BRR = 2 (0x02)
        flag_byte = 3 if meta["has_loop"] else 2

        if current_write_offset + len(brr_data) > SOUNDBANK_SIZE:
            print(f"⚠️ Warning: Soundbank limit breached! Dropping instrument slot [{slot:02d}]")
            continue

        # Commit entry to the 7-byte look-up configuration table array layout
        registry[slot] = [
            current_write_offset, # File absolute starting pointer
            total_blocks,         # Length in terms of compressed blocks
            loop_block_offset,    # Block index loop trigger anchor
            flag_byte             # Engine playback driver flag configuration
        ]

        payload_accumulator.extend(brr_data)
        print(f" Compressed slot [{slot:02d}]: Blocks: {total_blocks:04d} | LoopBlock: {loop_block_offset:04d} | Flags: {flag_byte}")

    # Build and finalize the binary soundbank image (.bin)
    with open(output_bin_path, "wb") as f:
        # 1. Write the 448-byte hardware header map
        for entry in registry:
            f.write(struct.pack("<HHHB", entry[0], entry[1], entry[2], entry[3]))
        
        # 2. Append compressed payloads
        f.write(payload_accumulator)

        # 3. Add safety pad block down to clear memory map boundaries
        bytes_written = f.tell()
        padding_needed = SOUNDBANK_SIZE - bytes_written
        if padding_needed > 0:
            f.write(b'\x00' * padding_needed)

    print(f"\nSuccessfully compiled BRR module assets!")
    print(f" -> Tracker Composition: {output_cm_path}")
    print(f" -> 64KB Compacted Soundbank: {output_bin_path} ({os.path.getsize(output_bin_path)} bytes)")

if __name__ == "__main__":
    # Point this to a standard 4-channel ProTracker .mod file!
    transpile_mod_to_brr(sys.argv[1], sys.argv[2], sys.argv[3])
