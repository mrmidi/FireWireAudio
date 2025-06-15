# Analyzer Test Suite

This directory contains comprehensive unit tests for the DebugTools/Analyzer module using pytest.

## Test Structure

```
tests/
├── __init__.py              # Test package initialization
├── conftest.py              # Shared fixtures and test configuration
├── test_analyzer.py         # Tests for Analyzer class
├── test_cip_packet.py       # Tests for CIPPacket class
├── test_log_parser.py       # Tests for log_parser module
└── README.md               # This file
```

## Coverage

The test suite covers the core functionality with high coverage:

- **analyzer.py**: 96% coverage (154/154 statements)
- **cip_packet.py**: 93% coverage (71/71 statements) 
- **log_parser.py**: 100% coverage (27/27 statements)

## Running Tests

### Prerequisites

Install testing dependencies:
```bash
pip install pytest pytest-cov
```

### Basic Usage

```bash
# Run all tests
pytest tests/

# Run with verbose output
pytest tests/ -v

# Run with coverage report
pytest tests/ --cov=firewire --cov-report=term-missing

# Run specific test file
pytest tests/test_analyzer.py

# Run specific test
pytest tests/test_analyzer.py::TestAnalyzer::test_dbc_continuity_correct_sequence
```

### Using Make

```bash
# Run tests
make test

# Run with coverage
make test-coverage

# Run verbose
make test-verbose

# Clean up test artifacts
make clean
```

### Using the Test Runner Script

```bash
python3 run_tests.py
```

## Test Categories

### CIPPacket Tests (`test_cip_packet.py`)
- Packet parsing (data vs no-data)
- Audio sample extraction and normalization
- CIP header field parsing
- 24-bit audio conversion
- Timestamp formatting
- Invalid packet handling

### Analyzer Tests (`test_analyzer.py`)
- Stream format analysis (44.1kHz, 48kHz, etc.)
- DBC continuity checking
- SYT timestamp analysis
- Audio waveform analysis
- Packet type classification
- Multi-channel support
- Pattern analysis

### Log Parser Tests (`test_log_parser.py`)
- FireBug log format parsing
- Multi-channel data handling
- Timestamp extraction
- Hex data processing
- Error handling

## Fixtures

The `conftest.py` provides shared test fixtures:

- `sample_data_packet`: Valid data packet for testing
- `sample_no_data_packet`: Valid no-data packet for testing
- `sample_44k_data`: Raw 44.1kHz FireBug log data
- `sample_48k_data`: Raw 48kHz FireBug log data
- `create_test_packet`: Factory for creating custom test packets

## Notes

- Tests use parametrization for testing multiple scenarios efficiently
- Real-world test data is used to ensure accuracy
- Error conditions and edge cases are thoroughly tested
- Tests follow PEP conventions and best practices