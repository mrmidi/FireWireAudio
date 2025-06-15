"""
Shared test fixtures for the Analyzer test suite.
"""
import pytest
import sys
import os
from pathlib import Path

# Add the project root to Python path for imports
project_root = Path(__file__).parent.parent
sys.path.insert(0, str(project_root))

from firewire.cip_packet import CIPPacket
from firewire.analyzer import Analyzer
from firewire.log_parser import parse_log_content


@pytest.fixture
def sample_data_packet():
    """Create a valid data packet for testing."""
    raw_packet = {
        'channel': 0,
        'tag': 1,
        'sy': 0,
        'size': 72,
        'actual_size': 72,
        'hexData': '000200c8 900108a7 40fa7401 4002acff 40fabd01 400383ff',
        'timestamp_cycle': 36,
        'timestamp_second': 1902,
        'timestamp_count': 14
    }
    return CIPPacket(raw_packet)


@pytest.fixture
def sample_no_data_packet():
    """Create a valid no-data packet for testing."""
    raw_packet = {
        'channel': 0,
        'tag': 1,
        'sy': 0,
        'size': 8,
        'actual_size': 8,
        'hexData': '000200c0 9001ffff',
        'timestamp_cycle': 36,
        'timestamp_second': 1900,
        'timestamp_count': 68
    }
    return CIPPacket(raw_packet)


@pytest.fixture
def sample_44k_data():
    """Sample 44.1kHz FireBug log data."""
    return """Apple FireBug 2.3 05.04.01

036:1900:3068  Isoch channel 0, tag 1, sy 0, size 8 [actual 8] s400
               0000   000200c0 9001ffff                     ........
036:1902:0014  Isoch channel 0, tag 1, sy 0, size 72 [actual 72] s400
               0000   000200c8 900108a7 40fa7401 4002acff   ........@.t.@...
               0010   40fabd01 400383ff 40fa4401 4002acff   @...@...@.D.@...
               0020   40f92c01 40ffae01 40f6af01 40fbd701   @.,.@...@...@...
               0030   40f48f01 40f96401 40f46a01 40f90601   @...@.d.@.j.@...
               0040   40f5ed01 40f97d01                     @...@.}.
036:1903:0006  Isoch channel 0, tag 1, sy 0, size 8 [actual 8] s400
               0000   000200c8 9001ffff                     ........"""


@pytest.fixture
def sample_48k_data():
    """Sample 48kHz FireBug log data."""
    return """Apple FireBug 2.3 05.04.01

079:5807:0722  Isoch channel 0, tag 1, sy 0, size 8 [actual 8] s400
               0000   020200e8 9002ffff                     ........
079:5808:0750  Isoch channel 0, tag 1, sy 0, size 72 [actual 72] s400
               0000   020200e8 9002e970 40000054 40000022   .......p@..T@.."
               0010   4000001c 4000006f 40000024 40000045   @...@..o@..$@..E
               0020   40000050 40000027 40000045 4000004a   @..P@..'@..E@..J
               0030   40000011 4000005a 40000032 40000031   @...@..Z@..2@..1
               0040   4000004c 40000054                     @..L@..T"""


@pytest.fixture
def test_data_directory():
    """Path to test data directory."""
    return project_root / "test_data"


@pytest.fixture
def create_test_packet():
    """Factory fixture for creating test packets."""
    def _create_packet(dbc, syt=0x0844, is_data=True, channel=0):
        if is_data:
            hex_data = f"00020{dbc:03x} 9001{syt:04x} 40fcbb71 40f86935 40f87261 40f6b0ea"
            size = 72
        else:
            hex_data = f"00020{dbc:03x} 9001ffff"
            size = 8
        
        raw_packet = {
            'channel': channel,
            'tag': 1,
            'sy': 0,
            'size': size,
            'actual_size': size,
            'hexData': hex_data,
            'timestamp_cycle': 44,
            'timestamp_second': 5099,
            'timestamp_count': 1312
        }
        return CIPPacket(raw_packet)
    
    return _create_packet