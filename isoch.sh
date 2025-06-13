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
            if [[ "$MODE" != "isoch" && "$MODE" != "fwa" && "$MODE" != "modified" && "$MODE" != "swift" && "$MODE" != "driver" && "$MODE" != "xpc" && "$MODE" != "transmit" && "$MODE" != "optimize" ]]; then
                echo "Error: Mode must be either 'isoch', 'fwa', 'modified', 'swift', 'driver', 'xpc', 'transmit', or 'optimize'"
                exit 1
            fi
            # Set default output file based on mode if not explicitly specified
            if [[ "$OUTPUT_FILE" == "isoch.txt" && "$MODE" == "fwa" ]]; then
                OUTPUT_FILE="fwa.txt"
            elif [[ "$OUTPUT_FILE" == "isoch.txt" && "$MODE" == "modified" ]]; then
                OUTPUT_FILE="modified.txt"
            elif [[ "$OUTPUT_FILE" == "isoch.txt" && "$MODE" == "swift" ]]; then
                OUTPUT_FILE="swift.txt"
            elif [[ "$OUTPUT_FILE" == "isoch.txt" && "$MODE" == "driver" ]]; then
                OUTPUT_FILE="driver.txt"
            elif [[ "$OUTPUT_FILE" == "isoch.txt" && "$MODE" == "xpc" ]]; then
                OUTPUT_FILE="xpc.txt"
            elif [[ "$OUTPUT_FILE" == "isoch.txt" && "$MODE" == "transmit" ]]; then
                OUTPUT_FILE="transmit.txt"
            elif [[ "$OUTPUT_FILE" == "isoch.txt" && "$MODE" == "optimize" ]]; then
                OUTPUT_FILE="optimize.txt"
            fi
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [-m|--mode mode] [-e|--extract file_to_extract] [-i|--input input_file] [-o|--output output_file]"
            echo "  -m, --mode       Specify the code stack to gather: 'isoch' (default), 'fwa', 'modified', 'swift', 'driver', 'xpc', 'transmit', or 'optimize'"
            echo "  -e, --extract    Specify a file to extract from the input file"
            echo "  -i, --input      Specify the input file (default: isoch.txt, fwa.txt, or modified.txt based on mode)"
            echo "  -o, --output     Specify the output file (default: isoch.txt, fwa.txt, or modified.txt based on mode)"
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

# Modified mode: collect all modified files from git status
if [[ "$MODE" == "modified" ]]; then
    rm -f "$OUTPUT_FILE"
    echo "Creating $OUTPUT_FILE with all modified and untracked files from git..."
    git status --porcelain | grep -E '^( M|M |A |AM|MM|\?\?)' | cut -c4- | while read -r file; do
        if [ -f "$file" ]; then
            echo "=== $file ===" >> "$OUTPUT_FILE"
            cat "$file" >> "$OUTPUT_FILE"
            echo -e "\n\n" >> "$OUTPUT_FILE"
        fi
    done
    chmod +x "$0"
    echo "Done! Created $OUTPUT_FILE"
    exit 0
fi

# Swift mode: gather all .swift, .cpp, .m, .mm, .hpp, .h files from FWA-Control, src/shared, and include/shared
if [[ "$MODE" == "swift" ]]; then
    rm -f "$OUTPUT_FILE"
    echo "Creating $OUTPUT_FILE with all Swift, C++, and ObjC source files from FWA-Control, src/shared, and include/shared..."
    SWIFT_DIRS=("FWA-Control" "src/shared" "include/shared")
    for DIR in "${SWIFT_DIRS[@]}"; do
        if [ -d "$DIR" ]; then
            find "$DIR" -type f \( -name "*.swift" -o -name "*.cpp" -o -name "*.m" -o -name "*.mm" -o -name "*.hpp" -o -name "*.h" \) | while read -r file; do
                echo "=== $file ===" >> "$OUTPUT_FILE"
                cat "$file" >> "$OUTPUT_FILE"
                echo -e "\n\n" >> "$OUTPUT_FILE"
            done
        else
            echo "Warning: $DIR directory not found!"
        fi
    done
    chmod +x "$0"
    echo "Done! Created $OUTPUT_FILE"
    exit 0
fi

# Add xpc mode: gather all .m, .mm, .h files from src/xpc and include/xpc
if [[ "$MODE" == "xpc" ]]; then
    rm -f "$OUTPUT_FILE"
    echo "Creating $OUTPUT_FILE with all XPC source and header files..."
    for DIR in "src/xpc" "include/xpc"; do
        if [ -d "$DIR" ]; then
            find "$DIR" -type f \( -name "*.m" -o -name "*.mm" -o -name "*.h" -o -name "*.hpp" -o -name "*.cpp" \) | while read -r file; do
                echo "=== $file ===" >> "$OUTPUT_FILE"
                cat "$file" >> "$OUTPUT_FILE"
                echo -e "\n\n" >> "$OUTPUT_FILE"
            done
        else
            echo "Warning: $DIR directory not found!"
        fi
    done
    chmod +x "$0"
    echo "Done! Created $OUTPUT_FILE"
    exit 0
fi

# Transmit mode: gather specific transmission-related files
if [[ "$MODE" == "transmit" ]]; then
    rm -f "$OUTPUT_FILE"
    echo "Creating $OUTPUT_FILE with transmission-related source files..."
    
    # Define the specific files to include
    TRANSMIT_FILES=(
        "include/shared/SharedMemoryStructures.hpp"
        "include/Isoch/core/AmdtpTransmitter.hpp"
        "include/Isoch/core/IsochPacketProvider.hpp"
        "include/Isoch/utils/TimingUtils.hpp"
        "src/Isoch/core/IsochPacketProvider.cpp"
        "src/Isoch/core/AmdtpTransmitter.cpp"
        "src/Isoch/core/IsochTransmitDCLManager.cpp"
        "src/Isoch/core/IsochTransmitBufferManager.cpp"
        "src/Driver/FWADriverHandler.cpp"
    )
    
    # Process each file
    for file in "${TRANSMIT_FILES[@]}"; do
        if [ -f "$file" ]; then
            echo "=== $file ===" >> "$OUTPUT_FILE"
            cat "$file" >> "$OUTPUT_FILE"
            echo -e "\n\n" >> "$OUTPUT_FILE"
        else
            echo "Warning: $file not found!"
        fi
    done
    
    chmod +x "$0"
    echo "Done! Created $OUTPUT_FILE"
    exit 0
fi

# Optimize mode: gather specific optimization-related files
if [[ "$MODE" == "optimize" ]]; then
    rm -f "$OUTPUT_FILE"
    echo "Creating $OUTPUT_FILE with optimization-related source files..."
    
    # Define the specific files to include for optimization work
    OPTIMIZE_FILES=(
        "src/Driver/FWADriver.cpp"
        "src/Driver/FWADriverDevice.cpp"
        "include/Isoch/core/IsochPacketProvider.hpp"
        "src/Isoch/core/IsochPacketProvider.cpp"
        "include/shared/SharedMemoryStructures.hpp"
        "src/Driver/FWAStream.hpp"
        "src/Driver/FWAStream.cpp"
        "src/Driver/FWADriverHandler.hpp"
        "src/Driver/FWADriverHandler.cpp"
    )
    
    # Process each file
    for file in "${OPTIMIZE_FILES[@]}"; do
        if [ -f "$file" ]; then
            echo "=== $file ===" >> "$OUTPUT_FILE"
            cat "$file" >> "$OUTPUT_FILE"
            echo -e "\n\n" >> "$OUTPUT_FILE"
        else
            echo "Warning: $file not found!"
        fi
    done
    
    chmod +x "$0"
    echo "Done! Created $OUTPUT_FILE"
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
elif [[ "$MODE" == "driver" ]]; then
    SRC_DIR="src/driver"
    INCLUDE_DIR=""
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
find "$dir" -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.mm" -o -name "*.m" \) | while read -r file; do
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
elif [[ "$MODE" == "driver" ]]; then
  echo "Creating $OUTPUT_FILE with driver source code..."
fi

# Always include shared directories
SHARED_SRC_DIR="src/shared"
SHARED_INCLUDE_DIR="include/shared"

# List of directories to process
DIRS_TO_PROCESS=()
if [ -n "$SRC_DIR" ]; then
  DIRS_TO_PROCESS+=("$SRC_DIR")
fi
if [ -n "$INCLUDE_DIR" ]; then
  DIRS_TO_PROCESS+=("$INCLUDE_DIR")
fi
DIRS_TO_PROCESS+=("$SHARED_SRC_DIR" "$SHARED_INCLUDE_DIR")

for DIR in "${DIRS_TO_PROCESS[@]}"; do
  if [ -d "$DIR" ]; then
    process_directory "$DIR"
  else
    echo "Warning: $DIR directory not found!"
  fi
done

# Make the script executable
chmod +x "$0"

echo "Done! Created $OUTPUT_FILE"
