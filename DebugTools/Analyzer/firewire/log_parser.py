# firewire/log_parser.py
import re
from .cip_packet import CIPPacket

def parse_log_content(content: str) -> list[CIPPacket]:
    """
    Parses a string of Apple FireBug log data into a list of CIPPacket objects.
    """
    lines = content.split('\n')
    packets = []
    current_packet_dict = None
    hex_data = []

    # Updated regex for the specific FireBug format
    header_re = re.compile(
        r"^\d+:\d+:\d+\s+Isoch channel (\d+), tag (\d+), sy (\d+), size (\d+)\s+\[actual (\d+)\]"
    )
    hex_word_re = re.compile(r'\b[0-9a-fA-F]{8}\b')

    for line in lines:
        line = line.strip()
        header_match = header_re.match(line)

        if header_match:
            if current_packet_dict and hex_data:
                current_packet_dict['hexData'] = ' '.join(hex_data)
                packets.append(CIPPacket(current_packet_dict))
            
            current_packet_dict = {
                'channel': int(header_match.group(1)),
                'tag': int(header_match.group(2)),
                'sy': int(header_match.group(3)),
                'size': int(header_match.group(4)),
                'actual_size': int(header_match.group(5)),
            }
            hex_data = []
            continue

        if current_packet_dict:
            hex_words_in_line = hex_word_re.findall(line)
            if hex_words_in_line:
                hex_data.extend(hex_words_in_line)

    if current_packet_dict and hex_data:
        current_packet_dict['hexData'] = ' '.join(hex_data)
        packets.append(CIPPacket(current_packet_dict))
        
    return packets
