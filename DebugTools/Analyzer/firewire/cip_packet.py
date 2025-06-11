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
        self.audio_samples = np.array([])
        
        # --- Fields from CIP Header ---
        self.fmt = None
        self.fdf = None
        self.syt = None
        self.dbs = None
        self.dbc = None
        self.is_valid = False
        self.is_data_packet = False
        
        self._parse_header_and_payload()

    def _parse_header_and_payload(self):
        """Parses the CIP header and extracts audio samples."""
        hex_words = [h for h in self.hex_data.split(' ') if len(h) == 8]
        if len(hex_words) < 2:
            return  # Not enough data for a header

        try:
            cip_word1 = int(hex_words[0], 16)
            cip_word2 = int(hex_words[1], 16)

            self.fmt = (cip_word1 >> 24) & 0x3F
            self.dbs = (cip_word1 >> 8) & 0xFF
            self.dbc = cip_word1 & 0xFF
            self.fdf = (cip_word2 >> 16) & 0xFF
            self.syt = cip_word2 & 0xFFFF
            
            self.is_valid = True

            # Check for placeholder/no-data packets.
            # In FireBug logs, this is indicated by syt=0xFFFF.
            # Standard NO-DATA packets use fdf=0xFF. We check both.
            if self.syt == 0xFFFF or self.fdf == 0xFF:
                self.is_data_packet = False
                return

            self.is_data_packet = True
            
            # Extract audio samples (payload) assuming AM824 format
            audio_words = hex_words[2:]
            samples = []
            for hex_word in audio_words:
                word = int(hex_word, 16)
                audio_data_24bit = word & 0x00FFFFFF
                
                # Convert from 24-bit two's complement to signed integer
                signed_val = audio_data_24bit - 0x1000000 if audio_data_24bit >= 0x800000 else audio_data_24bit
                
                # Normalize to [-1.0, 1.0]
                normalized_sample = signed_val / 0x7FFFFF
                samples.append(normalized_sample)
            
            self.audio_samples = np.array(samples)
            
        except (ValueError, IndexError):
            self.is_valid = False

    def __repr__(self):
        return f"CIPPacket(ch={self.channel}, dbc={self.dbc}, syt=0x{self.syt:X}, samples={len(self.audio_samples)})"
