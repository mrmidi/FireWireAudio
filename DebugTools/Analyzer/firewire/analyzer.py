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
        for i, p in enumerate(self.all_packets):
            if not p.is_valid: continue
            p.original_index = i 
            if p.channel not in packets_by_channel:
                packets_by_channel[p.channel] = []
            packets_by_channel[p.channel].append(p)
        for channel_id, channel_packets in packets_by_channel.items():
            last_data_packet_dbc = None
            for packet in channel_packets:
                if packet.is_data_packet:
                    if last_data_packet_dbc is None:
                        last_data_packet_dbc = packet.dbc
                        continue
                    expected_dbc = (last_data_packet_dbc + dbc_increment) % 256
                    if packet.dbc != expected_dbc:
                        all_discontinuities.append({
                            "channel": channel_id, "packet_index": packet.original_index,
                            "previous_data_dbc": last_data_packet_dbc,
                            "expected_dbc": expected_dbc, "actual_dbc": packet.dbc
                        })
                    last_data_packet_dbc = packet.dbc
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