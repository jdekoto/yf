        
import json
import sys

# Virtual console hardware specifications
MAP_WIDTH = 256
MAP_HEIGHT = 192
OUTPUT_SIZE = MAP_WIDTH * MAP_HEIGHT


def bake_map(json_path, bin_path):
    try:
        with open(json_path, "r") as f:
            data = json.load(f)
    except Exception as e:
        print(f"Error reading JSON file: {e}")
        return

    tile_size = data.get("tileSize", 8)
    layers = data.get("layers", [])

    # Step 1: Gather all tiles and convert to grid coordinates
    all_tiles = []
    for layer in layers:
        for tile in layer.get("tiles", []):
            tile_id = int(tile.get("id", 0))
            tx = int(tile.get("x", 0))
            ty = int(tile.get("y", 0))
            all_tiles.append((tx, ty, tile_id))

    if not all_tiles:
        print("Error: No tile data found in map.json!")
        exit(1)

    # Step 2: Find the absolute top-leftmost tile drawn in the editor
    min_tx = min(t[0] for t in all_tiles)
    min_ty = min(t[1] for t in all_tiles)

    print("=== Toolchain Normalization ===")
    print(f"Detected editor drawing origin at: Tile ({min_tx}, {min_ty})")

    buffer = bytearray(OUTPUT_SIZE)
    tiles_baked = 0
    tiles_skipped = 0

    # Step 3: Shift all tiles relative to (0, 0) and bake into linear RAM
    for tx, ty, tile_id in all_tiles:
        norm_tx = tx - min_tx
        norm_ty = ty - min_ty
        
        # Safety bounds check against internal engine limits
        if 0 <= norm_tx < MAP_WIDTH and 0 <= norm_ty < MAP_HEIGHT:
            idx = norm_ty * MAP_WIDTH + norm_tx
            buffer[idx] = (tile_id + 1) & 0xFF
            tiles_baked += 1
        else:
            tiles_skipped += 1

    print(f"Successfully shifted and baked: {tiles_baked} tiles.")
    if tiles_skipped > 0:
        print(f"Warning: {tiles_skipped} tiles cut off (exceeded {MAP_WIDTH}x{MAP_HEIGHT} layout limit).")
    
    try:
        with open(bin_path, "wb") as f:
            f.write(buffer)
    except Exception as e:
        print(f"Error writing binary file: {e}")
        
if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python bake_map.py <input.json> <output.bin>")
    else:
        bake_map(sys.argv[1], sys.argv[2])
