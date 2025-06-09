#!/bin/bash

# Git Diff Extract Script
# Usage: ./git-diff-extract.sh <branch1> <branch2>
# Creates diff.txt with changes and source.txt with source code from branch1

# Check if both arguments are provided
if [ $# -ne 2 ]; then
    echo "Usage: $0 <branch1> <branch2>"
    echo "Example: $0 main feature-branch"
    exit 1
fi

BRANCH1="$1"
BRANCH2="$2"

# Verify that both branches exist
if ! git rev-parse --verify "$BRANCH1" >/dev/null 2>&1; then
    echo "Error: Branch '$BRANCH1' does not exist"
    exit 1
fi

if ! git rev-parse --verify "$BRANCH2" >/dev/null 2>&1; then
    echo "Error: Branch '$BRANCH2' does not exist"
    exit 1
fi

echo "Creating diff between $BRANCH1 and $BRANCH2..."

# Create diff.txt with all changes
echo "Generating diff.txt..."
git diff "$BRANCH1".."$BRANCH2" > diff.txt
echo "✓ diff.txt created with changes from $BRANCH1 to $BRANCH2"

# Get list of changed files
echo "Getting list of changed files..."
CHANGED_FILES=$(git diff --name-only "$BRANCH1".."$BRANCH2")

# Create source.txt with source code from branch1
echo "Generating source.txt from $BRANCH1..."
> source.txt  # Clear the file

if [ -n "$CHANGED_FILES" ]; then
    echo "$CHANGED_FILES" | while IFS= read -r file; do
        echo "Processing: $file"
        
        # Check if file exists in branch1
        if git cat-file -e "$BRANCH1:$file" 2>/dev/null; then
            echo "=== $file ===" >> source.txt
            git show "$BRANCH1:$file" >> source.txt
            echo "" >> source.txt
            echo "" >> source.txt
        else
            echo "=== $file ===" >> source.txt
            echo "# File does not exist in $BRANCH1 (newly added in $BRANCH2)" >> source.txt
            echo "" >> source.txt
            echo "" >> source.txt
        fi
    done
    echo "✓ source.txt created with source code from $BRANCH1"
else
    echo "No changed files found between $BRANCH1 and $BRANCH2"
    echo "# No changed files found between $BRANCH1 and $BRANCH2" > source.txt
fi

echo ""
echo "Files created:"
echo "- diff.txt: Contains the diff between $BRANCH1 and $BRANCH2"
echo "- source.txt: Contains source code of changed files from $BRANCH1"
echo ""
echo "Summary:"
echo "Changed files: $(echo "$CHANGED_FILES" | wc -l | tr -d ' ')"
echo "Diff size: $(wc -l < diff.txt) lines"
echo "Source size: $(wc -l < source.txt) lines"