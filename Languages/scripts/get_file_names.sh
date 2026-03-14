#!/bin/bash

# Check directory argument
if [ -z "$1" ]; then
    echo "Usage: $0 <directory>"
    exit 1
fi

DIR="$1"
OUTPUT="benchmark_inputs.txt"

# Verify directory exists
if [ ! -d "$DIR" ]; then
    echo "Error: Directory '$DIR' does not exist."
    exit 1
fi

# Temporary file for this run
tmpfile=$(mktemp)

# Extract first token before dot
for filepath in "$DIR"/*; do
    [ -e "$filepath" ] || continue
    filename="$(basename "$filepath")"
    echo "${filename%%.*}" >> "$tmpfile"
done

# Append to output
cat "$tmpfile" >> "$OUTPUT"

# Remove duplicates in the output file
sort -u "$OUTPUT" -o "$OUTPUT"

# Clean up temp
rm "$tmpfile"

echo "Appended new entries and removed duplicates in $OUTPUT"
