#!/bin/bash

# Remove code.txt if it exists
rm -f code.txt

# Find all .swift files recursively from the current directory
find . -type f -name "*.swift" ! -name "dummy.swift" | while read file; do
    echo "=== $file ===" >> code.txt
    cat "$file" >> code.txt
    echo "" >> code.txt
    echo "" >> code.txt
done
