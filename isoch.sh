#!/bin/bash
# This script is for gathering source code from the FireWire Audio project
# It can dump code from the isochronous stack (default) or FWA stack
# It can also extract specific files from the generated output

# Default values
MODE="isoch"  # Default mode is "isoch"
INPUT_FILE="isoch.txt"
OUTPUT_FILE="isoch.txt"
EXTRACT_FILE=""

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -e|--extract)
            EXTRACT_FILE="$2"
            shift 2
            ;;
        -i|--input)
            INPUT_FILE="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -m|--mode)
            MODE="$2"
            if [[ "$MODE" != "isoch" && "$MODE" != "fwa" ]]; then
                echo "Error: Mode must be either 'isoch' or 'fwa'"
                exit 1
            fi
            # Set default output file based on mode if not explicitly specified
            if [[ "$OUTPUT_FILE" == "isoch.txt" && "$MODE" == "fwa" ]]; then
                OUTPUT_FILE="fwa.txt"
            fi
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [-m|--mode mode] [-e|--extract file_to_extract] [-i|--input input_file] [-o|--output output_file]"
            echo "  -m, --mode       Specify the code stack to gather: 'isoch' (default) or 'fwa'"
            echo "  -e, --extract    Specify a file to extract from the input file"
            echo "  -i, --input      Specify the input file (default: isoch.txt or fwa.txt based on mode)"
            echo "  -o, --output     Specify the output file (default: isoch.txt or fwa.txt based on mode)"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [-m|--mode mode] [-e|--extract file_to_extract] [-i|--input input_file] [-o|--output output_file]"
            exit 1
            ;;
    esac
done

# Function to extract a specific file from isoch.txt
extract_file() {
    local search_file="$1"
    local input_file="$2"
    local output_file="$3"
    
    if [ ! -f "$input_file" ]; then
        echo "Error: Input file $input_file not found!"
        exit 1
    fi

    # Use awk to extract the content between === markers, matching the filename anywhere in the marker line
    awk -v search="$search_file" '
        $0 ~ /^=== .*\// && $0 ~ search {
            found = 1
            print
            next
        }
        /^=== / {
            if (found) exit
        }
        found {
            print
        }
    ' "$input_file" > "$output_file"

    if [ ! -s "$output_file" ]; then
        echo "Error: File $search_file not found in $input_file"
        rm -f "$output_file"
        exit 1
    fi
    
    echo "Successfully extracted $search_file to $output_file"
    exit 0
}

# If extraction is requested, do it and exit
if [ ! -z "$EXTRACT_FILE" ]; then
    extract_file "$EXTRACT_FILE" "$INPUT_FILE" "$OUTPUT_FILE"
    exit 0
fi

# Original functionality starts here
# Remove the output file if it already exists
rm -f "$OUTPUT_FILE"

# Define source and include directories based on the selected mode
if [[ "$MODE" == "isoch" ]]; then
    SRC_DIR="src/Isoch"
    INCLUDE_DIR="include/Isoch"
elif [[ "$MODE" == "fwa" ]]; then
    SRC_DIR="src/FWA"
    INCLUDE_DIR="include/FWA"
fi

# Define ignored folders and files
IGNORE_FOLDERS=("components")
IGNORE_FILES=( 
    "AmdtTransmitStreamProcessor.cpp" 
    "AmdtTransmitStreamProcessor.hpp" 
    "CIPHeaderHandler.cpp"
    "CIPHeaderHandler.hpp"
    "AmdtpHelpers.cpp"
    "AmdtpHelpers.hpp")

# Function to check if a file should be ignored
should_ignore() {
  local file=$(basename "$1")
  
  # Check if file is in the ignore list
  for ignore_file in "${IGNORE_FILES[@]}"; do
    if [[ "$file" == "$ignore_file" ]]; then
      return 0  # True, should ignore
    fi
  done
  
  return 1  # False, should not ignore
}

# Function to check if a path contains ignored folders
contains_ignored_folder() {
  local path="$1"
  
  for folder in "${IGNORE_FOLDERS[@]}"; do
    if [[ "$path" == *"/$folder/"* ]]; then
      return 0  # True, contains ignored folder
    fi
  done
  
  return 1  # False, does not contain ignored folder
}

# Function to process a directory
process_directory() {
  local dir="$1"
  
  # Find all .cpp and .hpp files in the directory and subdirectories
  find "$dir" -type f \( -name "*.cpp" -o -name "*.hpp" \) | while read -r file; do
    # Skip if the file should be ignored or is in an ignored folder
    if should_ignore "$file" || contains_ignored_folder "$file"; then
      continue
    fi
    
    # Append filename and content to output file
    echo "=== $file ===" >> "$OUTPUT_FILE"
    cat "$file" >> "$OUTPUT_FILE"
    echo -e "\n\n" >> "$OUTPUT_FILE"
  done
}

# Main execution
if [[ "$MODE" == "isoch" ]]; then
  echo "Creating $OUTPUT_FILE with isochronous stack source code..."
elif [[ "$MODE" == "fwa" ]]; then
  echo "Creating $OUTPUT_FILE with FWA stack source code..."
fi

# Process source directory
if [ -d "$SRC_DIR" ]; then
  process_directory "$SRC_DIR"
else
  echo "Warning: $SRC_DIR directory not found!"
fi

# Process include directory
if [ -d "$INCLUDE_DIR" ]; then
  process_directory "$INCLUDE_DIR"
else
  echo "Warning: $INCLUDE_DIR directory not found!"
fi

# Make the script executable
chmod +x "$0"

echo "Done! Created $OUTPUT_FILE"
