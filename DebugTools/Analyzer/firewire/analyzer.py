# firewire/analyzer.py (Updated)
import numpy as np
from scipy.fft import rfft, rfftfreq
import math

# The IEEE 1394 cycle timer frequency
CYCLE_TIMER_FREQ = 24_576_000

class Analyzer:
    def __init__(self, packets: list):
        self.all_packets = packets
        self.data_packets = [p for p in packets if p.is_valid and p.is_data_packet]
        self.sample_rate = 44100  # Default
        self.syt_interval = 8     # Default
        # Analyze format immediately to set the sample rate and SYT_INTERVAL
        self.format_info = self._analyze_stream_format()
        if "nominal_sample_rate" in self.format_info and isinstance(self.format_info["nominal_sample_rate"], int):
            self.sample_rate = self.format_info["nominal_sample_rate"]
        if "syt_interval" in self.format_info and self.format_info["syt_interval"] != "Unknown":
            self.syt_interval = self.format_info["syt_interval"]

    def _analyze_stream_format(self):
        """Analyzes FDF/SFC to determine stream properties and SYT_INTERVAL."""
        if not self.all_packets:
            return {"error": "No valid packets to analyze."}

        first_data_packet = next((p for p in self.all_packets if p.is_data_packet), None)
        if not first_data_packet:
             return {"error": "No data packets found to determine format."}

        fdf = first_data_packet.fdf
        sfc_code = fdf & 0b00000111
        # Mapping from GOST Table 20 (and IEC 61883-6)
        sfc_map = {0: 32000, 1: 44100, 2: 48000, 3: 88200, 4: 96000, 5: 176400, 6: 192000}
        syt_interval_map = {0: 8, 1: 8, 2: 8, 3: 16, 4: 16, 5: 32, 6: 32}
        rate = sfc_map.get(sfc_code, "Unknown")
        syt_interval = syt_interval_map.get(sfc_code, "Unknown")
        fdf_type_code = (fdf >> 4) & 0b00001111
        fdf_type_map = {0: "AM824", 1: "24-bit x 4 Audio", 2: "32-bit Float", 3: "32-bit Generic"}
        return {
            "fdf_full_hex": f"0x{fdf:02X}",
            "subformat_type": fdf_type_map.get(fdf_type_code, "Reserved"),
            "sfc_code": sfc_code,
            "nominal_sample_rate": rate,
            "syt_interval": syt_interval
        }
        
    def analyze_dbc_continuity(self):
        """Checks for discontinuities in the Data Block Counter (DBC) on a per-channel basis using correct SYT_INTERVAL."""
        all_discontinuities = []
        dbc_increment = self.syt_interval
        packets_by_channel = {}
        
        # Add packet indexing and DBC status for detailed display
        for i, p in enumerate(self.all_packets):
            if not p.is_valid: continue
            p.original_index = i 
            p.dbc_status = "unknown"  # Will be set to "correct", "incorrect", "first", or "no-data"
            if p.channel not in packets_by_channel:
                packets_by_channel[p.channel] = []
            packets_by_channel[p.channel].append(p)
            
        for channel_id, channel_packets in packets_by_channel.items():
            last_data_packet_dbc = None
            last_packet_dbc = None  # Track DBC of any packet (data or no-data)
            prev_packet_was_no_data = False
            
            for packet in channel_packets:
                if not packet.is_data_packet:
                    # No-data packets: DBC should increment by 8 from the last data packet
                    packet.dbc_status = "no-data"
                    if last_data_packet_dbc is not None:
                        expected_dbc = (last_data_packet_dbc + 8) % 256
                        if packet.dbc != expected_dbc:
                            packet.dbc_status = "incorrect"
                            all_discontinuities.append({
                                "channel": channel_id, "packet_index": packet.original_index,
                                "packet_type": "no-data",
                                "previous_data_dbc": last_data_packet_dbc,
                                "expected_dbc": expected_dbc, "actual_dbc": packet.dbc,
                                "description": f"No-data packet DBC should be {expected_dbc:02X} (last data + 8), got {packet.dbc:02X}"
                            })
                        else:
                            packet.dbc_status = "correct"
                    # Track this no-data packet's DBC but don't update last_data_packet_dbc
                    last_packet_dbc = packet.dbc
                    prev_packet_was_no_data = True
                else:
                    # Data packets
                    if last_data_packet_dbc is None:
                        # First data packet in sequence
                        last_data_packet_dbc = packet.dbc
                        last_packet_dbc = packet.dbc
                        packet.dbc_status = "first"
                        prev_packet_was_no_data = False
                        continue
                    
                    # Determine expected DBC for this data packet
                    if prev_packet_was_no_data:
                        # After no-data packet: DBC should be same as the no-data packet (no increment)
                        expected_dbc = last_packet_dbc
                    else:
                        # Normal data-to-data sequence: increment by SYT_INTERVAL from last data packet
                        expected_dbc = (last_data_packet_dbc + dbc_increment) % 256
                    
                    if packet.dbc != expected_dbc:
                        packet.dbc_status = "incorrect"
                        discontinuity_info = {
                            "channel": channel_id, "packet_index": packet.original_index,
                            "packet_type": "data",
                            "previous_data_dbc": last_data_packet_dbc,
                            "expected_dbc": expected_dbc, "actual_dbc": packet.dbc,
                            "after_no_data": prev_packet_was_no_data
                        }
                        if prev_packet_was_no_data:
                            discontinuity_info["description"] = f"Data packet after no-data should have DBC {expected_dbc:02X} (same as no-data), got {packet.dbc:02X}"
                        else:
                            discontinuity_info["description"] = f"Data packet should have DBC {expected_dbc:02X} (last data + {dbc_increment}), got {packet.dbc:02X}"
                        all_discontinuities.append(discontinuity_info)
                    else:
                        packet.dbc_status = "correct"
                    
                    # Update tracking variables
                    last_data_packet_dbc = packet.dbc
                    last_packet_dbc = packet.dbc
                    prev_packet_was_no_data = False
                    
        return {
            "total_packets": len(self.all_packets),
            "channels_found": list(packets_by_channel.keys()),
            "expected_increment": dbc_increment,
            "discontinuities": all_discontinuities
        }

    def analyze_syt_timestamp(self):
        """Analyzes SYT for timing information and jitter."""
        # ... (This function remains the same) ...
        if len(self.data_packets) < 2: return {"error": "Not enough data packets for SYT analysis."}
        syt_values = np.array([p.syt for p in self.data_packets])
        syt_deltas = np.diff(syt_values.astype(np.int32))
        syt_deltas = (syt_deltas + 65536) % 65536
        samples_per_packet = len(self.data_packets[0].audio_samples)
        if self.sample_rate > 0 and samples_per_packet > 0:
            theoretical_delta = samples_per_packet * (CYCLE_TIMER_FREQ / self.sample_rate)
        else:
            theoretical_delta = "N/A"

        return {
            "packet_count": len(self.data_packets), "theoretical_syt_delta_per_packet": theoretical_delta,
            "mean_syt_delta": np.mean(syt_deltas), "std_dev_syt_delta (jitter)": np.std(syt_deltas),
            "min_syt_delta": np.min(syt_deltas), "max_syt_delta": np.max(syt_deltas),
            "deltas_data": syt_deltas
        }


    def get_aggregated_audio_samples(self, channel_select=None):
        """Aggregates audio samples from data packets."""
        # ... (This function remains the same) ...
        all_samples = []
        for packet in self.data_packets:
            if channel_select is not None and packet.channel != channel_select:
                continue
            all_samples.append(packet.audio_samples)
        return np.concatenate(all_samples) if all_samples else np.array([])
    
    def analyze_audio_waveform(self, samples):
        """Performs analysis and returns results in a presentation-friendly format."""
        if len(samples) == 0: return {}
        rms_float = np.sqrt(np.mean(samples**2))
        peak_float = np.max(np.abs(samples))
        dc_offset = np.mean(samples)
        epsilon = 1e-9
        peak_dbfs = 20 * math.log10(peak_float + epsilon)
        rms_dbfs = 20 * math.log10(rms_float + epsilon)
        return {
            "sample_count": len(samples),
            "duration_seconds": len(samples) / self.sample_rate if self.sample_rate > 0 else 0,
            "peak_float": peak_float,
            "peak_dbfs": peak_dbfs,
            "rms_float": rms_float,
            "rms_dbfs": rms_dbfs,
            "crest_factor": peak_float / rms_float if rms_float > epsilon else 0,
            "dc_offset": dc_offset,
        }
    
    def analyze_packet_types(self):
        """Analyzes the distribution of data vs no-data packets."""
        valid_packets = [p for p in self.all_packets if p.is_valid]
        data_packets = [p for p in valid_packets if p.is_data_packet]
        no_data_packets = [p for p in valid_packets if not p.is_data_packet]
        
        # Validate no-data packets
        valid_no_data_packets = []
        invalid_no_data_packets = []
        
        for packet in no_data_packets:
            is_valid_no_data = True
            issues = []
            
            # Check SYT field (should be 0xFFFF for valid no-data packets)
            if packet.syt != 0xFFFF:
                is_valid_no_data = False
                issues.append(f"SYT should be 0xFFFF, got 0x{packet.syt:04X}")
            
            # Check SFC code in FDF (should match sample rate, not be 0xFF)
            sfc_code = packet.fdf & 0b00000111
            if packet.fdf == 0xFF:
                is_valid_no_data = False
                issues.append("FDF is 0xFF (invalid for proper no-data packets)")
            elif sfc_code not in [0, 1, 2, 3, 4, 5, 6]:
                is_valid_no_data = False
                issues.append(f"Invalid SFC code {sfc_code} in FDF")
            
            if is_valid_no_data:
                valid_no_data_packets.append(packet)
            else:
                packet.no_data_issues = issues
                invalid_no_data_packets.append(packet)
        
        total_valid = len(valid_packets)
        data_count = len(data_packets)
        no_data_count = len(no_data_packets)
        valid_no_data_count = len(valid_no_data_packets)
        
        return {
            "total_valid_packets": total_valid,
            "data_packets": data_count,
            "no_data_packets": no_data_count,
            "valid_no_data_packets": valid_no_data_count,
            "invalid_no_data_packets": len(invalid_no_data_packets),
            "data_rate_percent": (data_count / total_valid * 100) if total_valid > 0 else 0,
            "no_data_rate_percent": (no_data_count / total_valid * 100) if total_valid > 0 else 0,
            "valid_no_data_examples": valid_no_data_packets[:3],  # First 3 examples
            "invalid_no_data_examples": invalid_no_data_packets[:3]
        }
    
    def get_packet_pattern_analysis(self, num_packets=50):
        """
        Returns detailed packet pattern analysis including sequence and statistics.
        """
        valid_packets = [p for p in self.all_packets if p.is_valid][:num_packets]
        
        pattern_chars = []
        data_count = 0
        no_data_count = 0
        sequences = []
        current_sequence = []
        current_type = None
        
        for packet in valid_packets:
            packet_type = 'D' if packet.is_data_packet else 'N'
            pattern_chars.append(packet_type)
            
            if packet.is_data_packet:
                data_count += 1
            else:
                no_data_count += 1
            
            # Track sequences
            if packet_type == current_type:
                current_sequence.append(packet_type)
            else:
                if current_sequence:
                    sequences.append((''.join(current_sequence), len(current_sequence)))
                current_sequence = [packet_type]
                current_type = packet_type
        
        # Don't forget the last sequence
        if current_sequence:
            sequences.append((''.join(current_sequence), len(current_sequence)))
        
        return {
            "pattern": ' '.join(pattern_chars),
            "total_packets": len(valid_packets),
            "data_packets": data_count,
            "no_data_packets": no_data_count,
            "data_percentage": (data_count / len(valid_packets) * 100) if valid_packets else 0,
            "sequences": sequences,
            "pattern_chars": pattern_chars
        }