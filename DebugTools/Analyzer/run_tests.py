#!/usr/bin/env python3
"""
Test runner script for the Analyzer module.
"""
import sys
import subprocess
from pathlib import Path


def main():
    """Run the test suite."""
    print("🧪 Running Analyzer Test Suite")
    print("=" * 50)
    
    # Change to the script directory
    script_dir = Path(__file__).parent
    
    try:
        # Run pytest with coverage
        result = subprocess.run([
            sys.executable, "-m", "pytest",
            "tests/",
            "-v",
            "--cov=firewire",
            "--cov-report=term-missing"
        ], cwd=script_dir, capture_output=False)
        
        if result.returncode == 0:
            print("\n✅ All tests passed!")
        else:
            print(f"\n❌ Tests failed with exit code {result.returncode}")
            
        return result.returncode
        
    except FileNotFoundError:
        print("❌ pytest not found. Please install with: pip install pytest pytest-cov")
        return 1
    except Exception as e:
        print(f"❌ Error running tests: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())