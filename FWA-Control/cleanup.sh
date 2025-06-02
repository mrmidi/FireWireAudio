#!/bin/bash
# cleanup-legacy-code.sh
# Automated script to remove legacy FWA-Control files

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if we're in the right directory
if [ ! -f "OverviewView.swift" ] || [ ! -f "UIManager.swift" ]; then
    print_error "This script must be run from the FWA-Control directory"
    print_error "Expected files: OverviewView.swift, UIManager.swift"
    exit 1
fi

print_status "Starting legacy code cleanup in: $(pwd)"

# Create backup directory
BACKUP_DIR="legacy-backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP_DIR"
print_status "Created backup directory: $BACKUP_DIR"

# List of files to remove
LEGACY_FILES=(
    "DeviceManager.swift"
    "CAPICallbackHandler.swift"
    "HardwareInterface.swift"
    "ExpandableSectionView.swift"
    "DeviceRefreshButton.swift"
    "DriverInstallerService.swift"
    "Test.swift"
    "Package.swift"
    "FWAManagerBridge.swift"
)

# Check which files exist and back them up
print_status "Checking for legacy files..."
FILES_TO_REMOVE=()

for file in "${LEGACY_FILES[@]}"; do
    if [ -f "$file" ]; then
        print_warning "Found legacy file: $file"
        cp "$file" "$BACKUP_DIR/"
        FILES_TO_REMOVE+=("$file")
    else
        print_status "Legacy file not found (already removed?): $file"
    fi
done

if [ ${#FILES_TO_REMOVE[@]} -eq 0 ]; then
    print_status "No legacy files found to remove. Cleanup already complete!"
    rmdir "$BACKUP_DIR" 2>/dev/null || true
    exit 0
fi

# Show what will be removed
echo
print_warning "The following files will be REMOVED:"
for file in "${FILES_TO_REMOVE[@]}"; do
    echo "  - $file"
done

echo
print_warning "Files will be backed up to: $BACKUP_DIR"
echo

# Confirm with user
read -p "Do you want to proceed with the cleanup? (y/N): " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    print_status "Cleanup cancelled by user"
    rmdir "$BACKUP_DIR" 2>/dev/null || true
    exit 0
fi

# Remove the files
print_status "Removing legacy files..."
REMOVED_COUNT=0

for file in "${FILES_TO_REMOVE[@]}"; do
    if rm "$file" 2>/dev/null; then
        print_status "Removed: $file"
        ((REMOVED_COUNT++))
    else
        print_error "Failed to remove: $file"
    fi
done

# Handle FWAManagerBridge.swift extension migration
if [[ " ${FILES_TO_REMOVE[@]} " =~ " FWAManagerBridge.swift " ]]; then
    print_status "Migrating fwaCardStyle extension to SharedOverviewComponents.swift"
    
    if [ -f "SharedOverviewComponents.swift" ]; then
        # Check if extension already exists
        if ! grep -q "func fwaCardStyle()" "SharedOverviewComponents.swift"; then
            cat >> "SharedOverviewComponents.swift" << 'EOF'

// MARK: - Legacy Extension Migration

extension View {
    func fwaCardStyle() -> some View {
        self
            .padding(8)
            .background(.ultraThinMaterial)
            .cornerRadius(8)
            .shadow(radius: 2)
    }
}
EOF
            print_status "Added fwaCardStyle extension to SharedOverviewComponents.swift"
        else
            print_status "fwaCardStyle extension already exists in SharedOverviewComponents.swift"
        fi
    else
        print_warning "SharedOverviewComponents.swift not found - you may need to manually migrate fwaCardStyle extension"
    fi
fi

# Calculate cleanup statistics
TOTAL_FILES=${#LEGACY_FILES[@]}
echo
print_status "Cleanup Summary:"
print_status "  Total legacy files checked: $TOTAL_FILES"
print_status "  Files removed: $REMOVED_COUNT"
print_status "  Files backed up to: $BACKUP_DIR"

if [ -d "$BACKUP_DIR" ] && [ "$(ls -A $BACKUP_DIR 2>/dev/null)" ]; then
    BACKUP_SIZE=$(du -sh "$BACKUP_DIR" | cut -f1)
    print_status "  Backup size: $BACKUP_SIZE"
else
    rmdir "$BACKUP_DIR" 2>/dev/null || true
fi

# Check for potential import issues
print_status "Checking for potential import issues..."
IMPORT_ISSUES=()

for file in *.swift; do
    if [ -f "$file" ]; then
        for removed_file in "${FILES_TO_REMOVE[@]}"; do
            # Extract filename without extension for import checking
            module_name=$(basename "$removed_file" .swift)
            if grep -q "import.*$module_name" "$file" 2>/dev/null; then
                IMPORT_ISSUES+=("$file imports $module_name")
            fi
        done
    fi
done

if [ ${#IMPORT_ISSUES[@]} -gt 0 ]; then
    print_warning "Potential import issues found:"
    for issue in "${IMPORT_ISSUES[@]}"; do
        echo "  - $issue"
    done
    print_warning "Please review and update these imports manually"
else
    print_status "No obvious import issues detected"
fi

# Final recommendations
echo
print_status "Cleanup completed successfully!"
print_status "Recommended next steps:"
print_status "  1. Clean build in Xcode (⌘+Shift+K)"
print_status "  2. Rebuild project (⌘+B)"
print_status "  3. Test functionality"
print_status "  4. Commit changes to version control"
echo
print_warning "If you need to restore files, they are backed up in: $BACKUP_DIR"
print_warning "You can safely delete the backup directory after confirming everything works"

exit 0
