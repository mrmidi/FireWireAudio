#!/bin/bash

# Git Diff Extract Script
# Usage: ./git-diff-extract.sh <branch1> <branch2>
#    or: ./git-diff-extract.sh --current
# Creates diff.txt with changes and source.txt with source code from branch1 (or HEAD for --current)

# Check arguments
if [ $# -eq 1 ] && [ "$1" = "--current" ]; then
    # Use current uncommitted changes
    BRANCH1="HEAD"
    BRANCH2="working-directory"
    CURRENT_MODE=true
elif [ $# -ne 2 ]; then
    echo "Usage: $0 <branch1> <branch2>"
    echo "   or: $0 --current"
    echo "Example: $0 main feature-branch"
    echo "Example: $0 --current  # Compare uncommitted changes against HEAD"
    exit 1
else
    BRANCH1="$1"
    BRANCH2="$2"
    CURRENT_MODE=false
fi

# Verify branches exist (skip for --current mode)
if [ "$CURRENT_MODE" = false ]; then
    if ! git rev-parse --verify "$BRANCH1" >/dev/null 2>&1; then
        echo "Error: Branch '$BRANCH1' does not exist"
        exit 1
    fi

    if ! git rev-parse --verify "$BRANCH2" >/dev/null 2>&1; then
        echo "Error: Branch '$BRANCH2' does not exist"
        exit 1
    fi
fi

if [ "$CURRENT_MODE" = true ]; then
    echo "Creating diff for uncommitted changes against HEAD..."
else
    echo "Creating diff between $BRANCH1 and $BRANCH2..."
fi

# Create diff.txt with all changes
echo "Generating diff.txt..."
if [ "$CURRENT_MODE" = true ]; then
    # Include both staged and unstaged changes, plus new files
    git diff HEAD > diff.txt
    git diff --cached >> diff.txt
    echo "✓ diff.txt created with uncommitted changes against HEAD"
else
    git diff "$BRANCH1".."$BRANCH2" > diff.txt
    echo "✓ diff.txt created with changes from $BRANCH1 to $BRANCH2"
fi

# Get list of changed files
echo "Getting list of changed files..."
if [ "$CURRENT_MODE" = true ]; then
    # Get both modified and new files
    CHANGED_FILES=$(git diff --name-only HEAD; git diff --cached --name-only; git ls-files --others --exclude-standard)
    CHANGED_FILES=$(echo "$CHANGED_FILES" | sort -u)
else
    CHANGED_FILES=$(git diff --name-only "$BRANCH1".."$BRANCH2")
fi

# Create source.txt with source code from branch1
if [ "$CURRENT_MODE" = true ]; then
    echo "Generating source.txt from HEAD..."
else
    echo "Generating source.txt from $BRANCH1..."
fi
> source.txt  # Clear the file

if [ -n "$CHANGED_FILES" ]; then
    echo "$CHANGED_FILES" | while IFS= read -r file; do
        echo "Processing: $file"
        
        if [ "$CURRENT_MODE" = true ]; then
            # For current mode, check if file exists in HEAD or is a new file
            if git cat-file -e "HEAD:$file" 2>/dev/null; then
                echo "=== $file ===" >> source.txt
                git show "HEAD:$file" >> source.txt
                echo "" >> source.txt
                echo "" >> source.txt
            elif [ -f "$file" ]; then
                echo "=== $file ===" >> source.txt
                echo "# New file (does not exist in HEAD)" >> source.txt
                echo "" >> source.txt
                echo "" >> source.txt
            fi
        else
            # Original logic for branch comparison
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
        fi
    done
    
    if [ "$CURRENT_MODE" = true ]; then
        echo "✓ source.txt created with source code from HEAD"
    else
        echo "✓ source.txt created with source code from $BRANCH1"
    fi
else
    if [ "$CURRENT_MODE" = true ]; then
        echo "No uncommitted changes found"
        echo "# No uncommitted changes found" > source.txt
    else
        echo "No changed files found between $BRANCH1 and $BRANCH2"
        echo "# No changed files found between $BRANCH1 and $BRANCH2" > source.txt
    fi
fi

echo ""
echo "Files created:"
if [ "$CURRENT_MODE" = true ]; then
    echo "- diff.txt: Contains the diff of uncommitted changes against HEAD"
    echo "- source.txt: Contains source code of changed files from HEAD"
else
    echo "- diff.txt: Contains the diff between $BRANCH1 and $BRANCH2"
    echo "- source.txt: Contains source code of changed files from $BRANCH1"
fi
echo ""
echo "Summary:"
echo "Changed files: $(echo "$CHANGED_FILES" | wc -l | tr -d ' ')"
echo "Diff size: $(wc -l < diff.txt) lines"
echo "Source size: $(wc -l < source.txt) lines"
