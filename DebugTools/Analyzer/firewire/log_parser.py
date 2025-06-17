# firewire/log_parser.py
import re
from .cip_packet import CIPPacket

def parse_log_content(content: str) -> list[CIPPacket]:
    """
    Parses a string of Apple FireBug log data into a list of CIPPacket objects.
    Enhanced to detect LENGTH ERROR anomalies and other packet issues.
    """
    lines = content.split('\n')
    packets = []
    current_packet_dict = None
    hex_data = []

    # Updated regex for the specific FireBug format
    header_re = re.compile(
        r"^(\d+):(\d+):(\d+)\s+Isoch channel (\d+), tag (\d+), sy (\d+), size (\d+)\s+\[actual (\d+)\]"
    )
    hex_word_re = re.compile(r'\b[0-9a-fA-F]{8}\b')
    length_error_re = re.compile(r'LENGTH ERROR - Snooped (\d+) bytes')

    for line in lines:
        line = line.strip()
        header_match = header_re.match(line)
        length_error_match = length_error_re.search(line)

        if header_match:
            # Finalize previous packet if exists
            if current_packet_dict and hex_data:
                current_packet_dict['hexData'] = ' '.join(hex_data)
                packets.append(CIPPacket(current_packet_dict))
            
            current_packet_dict = {
                'timestamp_cycle': int(header_match.group(1)),
                'timestamp_second': int(header_match.group(2)),
                'timestamp_count': int(header_match.group(3)),
                'channel': int(header_match.group(4)),
                'tag': int(header_match.group(5)),
                'sy': int(header_match.group(6)),
                'size': int(header_match.group(7)),
                'actual_size': int(header_match.group(8)),
                'has_length_error': False,
                'length_error_bytes': 0
            }
            hex_data = []
            continue

        # Check for LENGTH ERROR messages
        if length_error_match and current_packet_dict:
            current_packet_dict['has_length_error'] = True
            current_packet_dict['length_error_bytes'] = int(length_error_match.group(1))
            # Continue parsing hex data from the raw quads that follow
            continue

        # Parse hex data
        if current_packet_dict:
            hex_words_in_line = hex_word_re.findall(line)
            if hex_words_in_line:
                hex_data.extend(hex_words_in_line)

    # Don't forget the last packet
    if current_packet_dict and hex_data:
        current_packet_dict['hexData'] = ' '.join(hex_data)
        packets.append(CIPPacket(current_packet_dict))
        
    return packets
