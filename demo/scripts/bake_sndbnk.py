import os
import sys
import wave
import struct

MAX_SOUND_SLOTS = 64
SOUNDBANK_SIZE = 64000  # Exactly 64KB
HEADER_ENTRY_SIZE = 7   # uint16, uint16, uint16, uint8
TOTAL_HEADER_SIZE = MAX_SOUND_SLOTS * HEADER_ENTRY_SIZE  # 448 bytes

def load_wav_samples(wav_path):
    """Reads a WAV file and returns a list of signed 16-bit linear PCM samples."""
    with wave.open(wav_path, 'rb') as w:
        n_channels = w.getnchannels()
        samp_width = w.getsampwidth()
        n_frames = w.getnframes()
        raw_frames = w.readframes(n_frames)
        
        samples = []
        for i in range(n_frames):
            frame_offset = i * n_channels * samp_width
            channel_data = raw_frames[frame_offset : frame_offset + samp_width]
            
            # Extract channel 0 and normalize to signed 16-bit range (-32768 to 32767)
            if samp_width == 1:
                # 8-bit WAV is unsigned [0, 255] -> convert to signed 16-bit
                val = (channel_data[0] - 128) << 8
            elif samp_width == 2:
                # 16-bit WAV is naturally signed short
                val = struct.unpack('<h', channel_data)[0]
            else:
                raise ValueError(f"Unsupported sample width: {samp_width} bytes")
                
            samples.append(val)
    return samples

def encode_brr_block(chunk):
    """Compresses a 16-sample chunk of 16-bit signed PCM into a 9-byte BRR block."""
    # Ensure chunk is exactly 16 samples long by zero-padding if necessary
    if len(chunk) < 16:
        chunk = chunk + [0] * (16 - len(chunk))
        
    # Find the optimum shift value (0 to 12) where all 4-bit steps fit into [-8, 7]
    best_shift = 12
    for shift in range(13):
        valid = True
        for sample in chunk:
            if shift == 0:
                step = sample << 1
            else:
                step = sample >> (shift - 1)
                
            if not (-8 <= step <= 7):
                valid = False
                break
        if valid:
            best_shift = shift
            break

    # Build the 1-byte BRR block header (Shift count, Filter 0, No loop flags)
    header_byte = (best_shift << 4) | (0 << 2)
    block_bytes = bytearray([header_byte])
    
    # Pack 16 samples into 8 bytes as pairs of 4-bit signed nibbles
    for i in range(0, 16, 2):
        s0, s1 = chunk[i], chunk[i+1]
        
        step0 = (s0 << 1) if best_shift == 0 else (s0 >> (best_shift - 1))
        step1 = (s1 << 1) if best_shift == 0 else (s1 >> (best_shift - 1))
        
        # Guard clamps
        step0 = max(-8, min(7, step0))
        step1 = max(-8, min(7, step1))
        
        # Convert signed values to 4-bit unsigned representations for bit-packing
        nibble0 = step0 & 0x0F
        nibble1 = step1 & 0x0F
        
        packed_byte = (nibble0 << 4) | nibble1
        block_bytes.append(packed_byte)
        
    return bytes(block_bytes)

def pack_brr_soundbank(source_dir, output_bin):
    print("--- Yellow Feather Core BRR Soundbank Builder ---")
    
    # [offset, length_in_blocks, loop_point, flags]
    registry = [[0, 0, 0, 0] for _ in range(MAX_SOUND_SLOTS)]
    payload_accumulator = bytearray()
    current_write_offset = TOTAL_HEADER_SIZE

    for slot_id in range(MAX_SOUND_SLOTS):
        filename = f"sfx_{slot_id:02d}.wav"
        filepath = os.path.join(source_dir, filename)

        if not os.path.exists(filepath):
            continue

        try:
            pcm_samples = load_wav_samples(filepath)
            if not pcm_samples:
                continue
                
            # Compress the linear PCM stream into sequential 9-byte BRR blocks
            brr_data = bytearray()
            for i in range(0, len(pcm_samples), 16):
                sample_chunk = pcm_samples[i : i + 16]
                brr_data.extend(encode_brr_block(sample_chunk))
                
            total_blocks = len(brr_data) // 9

            # Check if this addition breaches the 64KB soundbank constraint
            if current_write_offset + len(brr_data) > SOUNDBANK_SIZE:
                print(f"⚠️ ERROR: Soundbank full! Out of space to pack '{filename}'")
                return False

            # Assign header configuration parameters
            registry[slot_id] = [
                current_write_offset,  # Offset relative to file origin
                total_blocks,          # SPU expects total number of blocks for BRR sizing
                0,                     # Loop marker block index offset
                2                      # Flag value 2 = Active Hardware BRR Mode
            ]

            payload_accumulator.extend(brr_data)
            print(f" Packed slot [{slot_id:02d}]: '{filename}' -> {total_blocks} BRR blocks ({len(brr_data)} bytes)")
            
            current_write_offset += len(brr_data)

        except Exception as e:
            print(f"❌ Failed to process '{filename}': {e}")
            return False

    # --- SERIALIZE BINARY IMAGE ---
    with open(output_bin, "wb") as f:
        # 1. Write the 448-byte lookup index table matching struct pack specs
        for entry in registry:
            f.write(struct.pack("<HHHB", entry[0], entry[1], entry[2], entry[3]))

        # 2. Write compressed payloads
        f.write(payload_accumulator)

        # 3. Clean trailing pad straight to 64KB boundary with mid-point U8 silence (128)
        current_file_size = f.tell()
        padding_needed = SOUNDBANK_SIZE - current_file_size
        if padding_needed > 0:
            f.write(b'\x80' * padding_needed)
            
    print(f" Successfully compiled BRR soundbank: '{output_bin}' ({os.path.getsize(output_bin)} bytes)")
    return True

if __name__ == "__main__":
    SOURCE_DIRECTORY = sys.argv[1]
    OUTPUT_FILE = sys.argv[2]

    if not os.path.exists(SOURCE_DIRECTORY):
        os.makedirs(SOURCE_DIRECTORY)
        print(f"Created empty folder at '{SOURCE_DIRECTORY}'. Place 'sfx_00.wav' here and rerun.")
    else:
        pack_brr_soundbank(SOURCE_DIRECTORY, OUTPUT_FILE)
