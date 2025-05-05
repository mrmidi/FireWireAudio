#!/bin/bash

# Script to check if files are formatted correctly with clang-format
# Usage: ./tools/check-formatting.sh <clang-format-path> <file1> [<file2> ...]

# Get clang-format path from first argument
CLANG_FORMAT="$1"
shift

# Check if clang-format exists
if [ ! -x "$CLANG_FORMAT" ]; then
    echo "Error: clang-format not found at $CLANG_FORMAT or not executable"
    exit 1
fi

# Initialize error counter
ERRORS=0

# Check each file
for file in "$@"; do
    # Skip non-existent files
    if [ ! -f "$file" ]; then
        echo "Warning: File $file does not exist, skipping"
        continue
    fi
    
    # Create a temporary file
    TEMP_FILE=$(mktemp)
    
    # Format the file to the temporary file
    "$CLANG_FORMAT" "$file" > "$TEMP_FILE"
    
    # Compare the formatted file with the original
    if ! cmp -s "$file" "$TEMP_FILE"; then
        echo "Error: $file is not formatted correctly"
        # Show diff
        diff -u "$file" "$TEMP_FILE" | head -n 20
        ERRORS=$((ERRORS + 1))
    fi
    
    # Remove the temporary file
    rm "$TEMP_FILE"
done

# Report results
if [ $ERRORS -eq 0 ]; then
    echo "All files are formatted correctly"
    exit 0
else
    echo "Error: $ERRORS files are not formatted correctly"
    echo "Run 'cmake --build . --target format' to format all files"
    exit 1
fi
