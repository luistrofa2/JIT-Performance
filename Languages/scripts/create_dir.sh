#!/usr/bin/env bash

# Usage:
# ./organize_files.sh <directory> <extension> [suffix]

if [ -z "$1" ] || [ -z "$2" ]; then
  echo "Usage: $0 <directory> <extension> [suffix]"
  echo "Example: $0 dir1 dart _new"
  exit 1
fi

DIR="$1"
EXT="$2"
SUFFIX="${3:-}"   # optional suffix

cd "$DIR" || {
  echo "Error: cannot access directory '$DIR'"
  exit 1
}

shopt -s nullglob

for file in *."$EXT"; do
  # Folder name = filename without the final .<ext>
  folder="${file%.$EXT}"

  # New filename = substring before first dot + suffix + .<ext>
  base="${file%%.*}"
  new_name="${base}${SUFFIX}.${EXT}"

  mkdir -p "$folder"
  mv "$file" "$folder/$new_name"
done

