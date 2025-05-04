#!/bin/bash

# Script to check and install the required version of clang-format
# Usage: ./tools/install-clang-format.sh [--check-only]

# Required clang-format version
REQUIRED_VERSION="16.0.0"

# Check if running in check-only mode
CHECK_ONLY=false
if [ "$1" == "--check-only" ]; then
    CHECK_ONLY=true
fi

# Function to check if clang-format is installed and has the correct version
check_clang_format() {
    if ! command -v clang-format &> /dev/null; then
        echo "clang-format is not installed"
        return 1
    fi

    local version=$(clang-format --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
    if [ -z "$version" ]; then
        echo "Failed to determine clang-format version"
        return 1
    fi

    echo "Found clang-format version $version"
    
    # Compare versions
    if [ "$(printf '%s\n' "$REQUIRED_VERSION" "$version" | sort -V | head -n1)" != "$REQUIRED_VERSION" ]; then
        echo "clang-format version $version is older than required version $REQUIRED_VERSION"
        return 1
    fi
    
    if [ "$version" != "$REQUIRED_VERSION" ]; then
        echo "Warning: clang-format version $version is different from the required version $REQUIRED_VERSION"
        echo "This may cause formatting inconsistencies"
    fi
    
    return 0
}

# Check if clang-format is installed with the correct version
if check_clang_format; then
    echo "clang-format is properly installed"
    exit 0
fi

# Exit if we're only checking
if [ "$CHECK_ONLY" = true ]; then
    echo "clang-format check failed"
    exit 1
fi

# Install clang-format
echo "Installing clang-format version $REQUIRED_VERSION..."

# Detect OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    if command -v brew &> /dev/null; then
        echo "Installing via Homebrew..."
        brew install llvm@16
        echo "Creating symlink for clang-format..."
        ln -sf "$(brew --prefix llvm@16)/bin/clang-format" /usr/local/bin/clang-format
    else
        echo "Homebrew not found. Please install Homebrew first:"
        echo "  /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    # Linux
    if command -v apt-get &> /dev/null; then
        # Debian/Ubuntu
        echo "Installing via apt..."
        sudo apt-get update
        sudo apt-get install -y clang-format-16
        sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-16 100
    elif command -v dnf &> /dev/null; then
        # Fedora
        echo "Installing via dnf..."
        sudo dnf install -y clang-tools-extra
    elif command -v pacman &> /dev/null; then
        # Arch Linux
        echo "Installing via pacman..."
        sudo pacman -S clang
    else
        echo "Unsupported Linux distribution. Please install clang-format manually."
        exit 1
    fi
else
    echo "Unsupported operating system. Please install clang-format manually."
    exit 1
fi

# Check if installation was successful
if check_clang_format; then
    echo "clang-format was successfully installed"
    exit 0
else
    echo "Failed to install clang-format"
    exit 1
fi
