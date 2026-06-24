#!/usr/bin/env python3
import json
import sys
import os

# Yellow Feather Core Engine layout constraints
MAP_WIDTH = 512
MAP_HEIGHT = 256
OUTPUT_SIZE = MAP_WIDTH * MAP_HEIGHT

def compile_map(json_path, bin_path):
    if not os.path.exists(json_path):
        print(f"Error: Cannot find input file '{json_path}'")
        sys.exit(1)

    print(f"Reading {json_path}...")
    with open(json_path, 'r') as f:
        data = json.load(f)

    # Initialize a clean, flat 128KB map buffer filled with 0 (transparent/empty)
    out_buffer = bytearray(OUTPUT_SIZE)

    # Track structural dimensions exported from Sprite Fusion
    sf_width = data.get("mapWidth", 0)
    sf_height = data.get("mapHeight", 0)
    print(f"Sprite Fusion Map Dimensions: {sf_width}x{sf_height}")

    layers = data.get("layers", [])
    print(f"Processing {len(layers)} layer(s)...")

    # Process layers sequentially (higher layers overwrite lower layer tile values)
    for layer_idx, layer in enumerate(layers):
        tiles = layer.get("tiles", [])
        tiles_written = 0

        for tile in tiles:
            # Sprite Fusion coordinates
            tx = tile.get("x")
            ty = tile.get("y")
            
            # Convert string ID to int
            tile_id = int(tile.get("id"))

            # Core Shift Logic: 
            # Engine treats 0 as transparent and executes 'id - 1'.
            # Therefore, Sprite Fusion Tile 0 -> Engine Value 1 (draws sprite 0).
            engine_tile_val = tile_id + 1

            # Keep operations within safety limits of the engine's VRAM block boundaries
            if 0 <= tx < MAP_WIDTH and 0 <= ty < MAP_HEIGHT:
                dest_offset = ty * MAP_WIDTH + tx
                out_buffer[dest_offset] = engine_tile_val & 0xFF
                tiles_written += 1

        print(f"  -> Layer {layer_idx} ('{layer.get('name', 'Unknown')}'): Baked {tiles_written} tiles.")

    # Write out the final binary image
    print(f"Writing packed VRAM layout to {bin_path} ({OUTPUT_SIZE} bytes)...")
    with open(bin_path, 'wb') as f:
        f.write(out_buffer)
        
    print("Map compilation complete!")

if __name__ == "__main__":
    compile_map(sys.argv[1], sys.argv[2])   

