import sys
import struct

def generate_arp_test_module(output_filename):
    # Header Construction
    magic = b"CM20"           # Custom format verification signature tag
    bpm = 120
    speed = 6
    song_length = 1          # 1 Pattern tracking sequence length
    
    header = struct.pack("<4sBBH", magic, bpm, speed, song_length)
    
    # Order Pattern Mapping Table (128 bytes total space allocation)
    order_list = bytearray(128)
    order_list[0] = 0        # Sequence index 0 points directly to Pattern 0
    
    # Pattern Binary Generator (64 rows, 4 channels per row, 5 bytes per channel)
    # Total pattern footprint: 64 * 4 * 5 = 1280 bytes
    pattern_data = bytearray()
    
    # Define an active classic arpeggio run cycle (indices match note_pitch_table Octave 2)
    # 25 = C-2 (128 playback speed multiplier), 29 = E-2, 32 = G-2
    arp_notes = [25, 29, 32, 29]
    
    for row in range(64):
        row_data = bytearray()
        
        for ch in range(4):
            if ch == 0 and (row % 4 == 0):
                # Trigger a rolling arpeggio note every 4 steps on hardware voice 0
                arp_step = (row // 4) % len(arp_notes)
                note = arp_notes[arp_step]
                inst = 5        # Play via instrument index block 1
                vol = 220      # Define fixed raw step volume limit
                eff = 0
                param = 0
            else:
                # Default padded empty tracks context definitions
                note = 0
                inst = 0
                vol = 0xFF     # 0xFF informs the driver to maintain existing channel volume
                eff = 0
                param = 0
                
            row_data.extend(struct.pack("BBBBB", note, inst, vol, eff, param))
            
        pattern_data.extend(row_data)

    # Compile the final physical binary artifact out to the file system
    with open(output_filename, "wb") as f:
        f.write(header)
        f.write(order_list)
        f.write(pattern_data)

    print(f"✔️ Successfully compiled expanded layout v2 test file: '{output_filename}'")
    print(f"   Size: {len(header) + len(order_list) + len(pattern_data)} bytes.")

if __name__ == "__main__":
    generate_arp_test_module(sys.argv[1])
