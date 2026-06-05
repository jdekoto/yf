#!/bin/bash
# pack.sh
# not completed
# 1. Read metadata from config.txt
TITLE=$(grep "title" config.txt | cut -d'"' -f2)
AUTHOR=$(grep "author" config.txt | cut -d'"' -f2)
VERSION=$(grep "version" config.txt | cut -d'"' -f2)

OUTPUT_FILE="yf_demo.yfc"

echo "=== Compiling Full Directory Cartridge Pack ==="

# 2. Use your native system tar engine to serialize the exact folder tree structural layout
# This securely packs your runtime, external, sources, and assets subfolders!
tar -cf temporary_payload.tar boot.lua runtime/ external/ assets/ sources/

# 3. Create a compiled binary injection patch containing your "YFC!" system validation strings
# We use standard printf formatting to pad strings to their required structural widths
printf "YFC!%-32s%-32s%-8s" "$TITLE" "$AUTHOR" "$VERSION" > "$OUTPUT_FILE"

# 4. Append the compressed filesystem payload directly onto the tail of your header file
cat temporary_payload.tar >> "$OUTPUT_FILE"

# Clean up temporary staging structures
rm temporary_payload.tar

# Verify binary payload limits match your 16MB specifications threshold
FILE_SIZE=$(wc -c < "$OUTPUT_FILE")
if [ $FILE_SIZE -gt 16777216 ]; then
    echo "CRITICAL COMPILER ERROR: Output binary exceeds maximum 16MB threshold limit!"
    exit 1
fi

echo "SUCCESS! Bundled file directory paths safely inside '$OUTPUT_FILE' ($FILE_SIZE bytes)"
