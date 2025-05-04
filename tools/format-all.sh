#!/bin/bash

# Script to format all source files in the project using clang-format
# Usage: ./tools/format-all.sh [--check-only]

# Check if running in check-only mode
CHECK_ONLY=false
if [ "$1" == "--check-only" ]; then
    CHECK_ONLY=true
fi

# Find clang-format
CLANG_FORMAT=$(command -v clang-format)
if [ -z "$CLANG_FORMAT" ]; then
    echo "Error: clang-format not found in PATH"
    echo "Please install clang-format or run ./tools/install-clang-format.sh"
    exit 1
fi

# Get the project root directory (where this script is located)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Find all source files
SOURCE_FILES=$(find "$PROJECT_ROOT/src" "$PROJECT_ROOT/include" -type f \( -name "*.cpp" -o -name "*.c" -o -name "*.h" -o -name "*.hpp" -o -name "*.mm" -o -name "*.m" \))

# Count the number of files
NUM_FILES=$(echo "$SOURCE_FILES" | wc -l)
echo "Found $NUM_FILES source files to format"

if [ "$CHECK_ONLY" = true ]; then
    echo "Checking formatting..."
    
    # Initialize error counter
    ERRORS=0
    
    # Check each file
    for file in $SOURCE_FILES; do
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
        echo "Run './tools/format-all.sh' to format all files"
        exit 1
    fi
else
    echo "Formatting all source files..."
    
    # Format each file
    echo "$SOURCE_FILES" | xargs "$CLANG_FORMAT" -i
    
    echo "Formatting complete"
    exit 0
fi
