# firewire/cip_packet.py
import numpy as np

class CIPPacket:
    """
    Represents and parses a single Common Isochronous Packet (CIP)
    optimized for Apple FireBug log format as per IEC 61883-6.
    """
    def __init__(self, raw_packet_dict):
        self.channel = raw_packet_dict.get('channel')
        self.tag = raw_packet_dict.get('tag')
        self.sy = raw_packet_dict.get('sy')
        self.header_size = raw_packet_dict.get('size')
        self.actual_size = raw_packet_dict.get('actual_size')
        self.hex_data = raw_packet_dict.get('hexData', '')
        
        # LENGTH ERROR detection
        self.has_length_error = raw_packet_dict.get('has_length_error', False)
        self.length_error_bytes = raw_packet_dict.get('length_error_bytes', 0)
        
        # FireBug timestamp information
        self.timestamp_cycle = raw_packet_dict.get('timestamp_cycle')
        self.timestamp_second = raw_packet_dict.get('timestamp_second') 
        self.timestamp_count = raw_packet_dict.get('timestamp_count')
        
        # Store raw packet info for detailed display
        self.raw_packet_dict = raw_packet_dict
        self.hex_words = []
        self.audio_samples = np.array([])
        self.audio_samples_hex = []  # Store original hex values for display
        
        # --- Fields from CIP Header ---
        self.fmt = None
        self.fdf = None
        self.syt = None
        self.dbs = None
        self.dbc = None
        self.is_valid = False
        self.is_data_packet = False
        self.samples_are_zero = False
        
        self._parse_header_and_payload()

    def _parse_header_and_payload(self):
        """Parses the CIP header and extracts audio samples."""
        self.hex_words = [h for h in self.hex_data.split(' ') if len(h) == 8]
        if len(self.hex_words) < 2:
            return  # Not enough data for a header

        try:
            cip_word1 = int(self.hex_words[0], 16)
            cip_word2 = int(self.hex_words[1], 16)

            self.fmt = (cip_word1 >> 24) & 0x3F
            self.dbs = (cip_word1 >> 8) & 0xFF
            self.dbc = cip_word1 & 0xFF
            self.fdf = (cip_word2 >> 16) & 0xFF
            self.syt = cip_word2 & 0xFFFF
            
            self.is_valid = True

            # Check for placeholder/no-data packets.
            # Valid no-data packets should have syt=0xFFFF and proper SFC in FDF
            # Invalid placeholders might use fdf=0xFF
            if self.syt == 0xFFFF:
                self.is_data_packet = False
                return
            elif self.fdf == 0xFF:
                # Legacy/invalid no-data packet format
                self.is_data_packet = False
                return

            self.is_data_packet = True
            
            # Extract audio samples (payload) assuming AM824 format
            audio_words = self.hex_words[2:]
            self.audio_samples_hex = audio_words  # Store for display
            samples = []
            all_zero = True
            
            for hex_word in audio_words:
                word = int(hex_word, 16)
                if word != 0:
                    all_zero = False
                    
                audio_data_24bit = word & 0x00FFFFFF
                
                # Convert from 24-bit two's complement to signed integer
                signed_val = audio_data_24bit - 0x1000000 if audio_data_24bit >= 0x800000 else audio_data_24bit
                
                # Normalize to [-1.0, 1.0]
                normalized_sample = signed_val / 0x7FFFFF
                samples.append(normalized_sample)
            
            self.samples_are_zero = all_zero
            self.audio_samples = np.array(samples)
            
        except (ValueError, IndexError):
            self.is_valid = False

    def get_cip_header_breakdown(self):
        """Returns a dictionary with CIP header fields for detailed display."""
        if not self.is_valid:
            return None
            
        return {
            'fmt': f"0x{self.fmt:02X}",
            'dbs': f"0x{self.dbs:02X}",
            'dbc': f"0x{self.dbc:02X}",
            'fdf': f"0x{self.fdf:02X}",
            'syt': f"0x{self.syt:04X}",
            'word1': self.hex_words[0] if len(self.hex_words) > 0 else "",
            'word2': self.hex_words[1] if len(self.hex_words) > 1 else ""
        }

    def get_timestamp_string(self):
        """Returns formatted timestamp string like FireBug format."""
        if all(x is not None for x in [self.timestamp_cycle, self.timestamp_second, self.timestamp_count]):
            return f"{self.timestamp_cycle:03d}:{self.timestamp_second:04d}:{self.timestamp_count:04d}"
        return "Unknown"

    def __repr__(self):
        return f"CIPPacket(ch={self.channel}, dbc={self.dbc}, syt=0x{self.syt:X}, samples={len(self.audio_samples)})"
