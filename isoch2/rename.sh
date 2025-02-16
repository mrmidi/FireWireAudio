#!/bin/bash

# Function to convert kebab-case to CamelCase
to_camel_case() {
    local input="$1"
    # Convert first letter of string and after each dash to uppercase
    # then remove all dashes
    echo "$input" | awk -F"-" '{
        result = "";
        for(i=1; i<=NF; i++) {
            substr1 = substr($i, 1, 1);
            substr2 = substr($i, 2);
            result = result toupper(substr1) substr2;
        }
        print result;
    }'
}

# Convert all files in the current directory from kebab-case to CamelCase
for file in *; do
    # Skip directories, the script itself, and files that don't have dashes
    if [ -f "$file" ] && [ "$file" != "rename.sh" ] && [[ "$file" == *-* ]]; then
        # Check for special case endings like "-cpp" or "-hpp"
        if [[ "$file" == *-cpp ]]; then
            base_name="${file%-cpp}"
            new_name="$(to_camel_case "$base_name").cpp"
        elif [[ "$file" == *-hpp ]]; then
            base_name="${file%-hpp}"
            new_name="$(to_camel_case "$base_name").hpp"
        elif [[ "$file" == *-fixed-callback-methods ]]; then
            base_name="${file%-fixed-callback-methods}"
            new_name="$(to_camel_case "$base_name")FixedCallbackMethods.cpp"
        else
            # Get extension (if it exists)
            extension="${file##*.}"
            
            if [[ "$file" == *.$extension && "$extension" != "$file" ]]; then
                # File has standard extension
                base_name="${file%.*}"
                new_name="$(to_camel_case "$base_name").$extension"
            else
                # Default case
                new_name="$(to_camel_case "$file")"
            fi
        fi
        
        # Rename the file
        echo "Renaming '$file' to '$new_name'"
        mv "$file" "$new_name"
    fi
done

echo "All files have been renamed to CamelCase format."