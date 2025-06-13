# firewire/audio_anomaly_analyzer.py
import numpy as np
from scipy.fft import rfft, rfftfreq
from scipy import signal
import math
from typing import List, Dict, Any, Tuple

class AudioAnomalyAnalyzer:
    """
    Advanced audio anomaly detection for FireWire audio streams.
    Focuses on correlating DBC discontinuities with actual audio quality issues.
    """
    
    def __init__(self, analyzer):
        self.analyzer = analyzer
        self.sample_rate = analyzer.sample_rate
        
    def trim_edge_packets(self, packets: List, trim_start: int = 2, trim_end: int = 2) -> List:
        """
        Removes potentially corrupted packets from capture boundaries.
        """
        if len(packets) <= (trim_start + trim_end):
            return packets  # Don't trim if too few packets
            
        trimmed = packets[trim_start:-trim_end] if trim_end > 0 else packets[trim_start:]
        return trimmed
    
    def get_clean_audio_samples(self, channel_select=None, trim_edges=True) -> Tuple[np.ndarray, List]:
        """
        Extracts audio samples with optional edge trimming and returns both samples and source packets.
        """
        data_packets = [p for p in self.analyzer.all_packets if p.is_valid and p.is_data_packet]
        
        if channel_select is not None:
            data_packets = [p for p in data_packets if p.channel == channel_select]
        
        if trim_edges:
            data_packets = self.trim_edge_packets(data_packets)
        
        all_samples = []
        for packet in data_packets:
            all_samples.append(packet.audio_samples)
            
        samples = np.concatenate(all_samples) if all_samples else np.array([])
        return samples, data_packets
    
    def analyze_inter_packet_discontinuities(self, channel_select=None) -> Dict[str, Any]:
        """
        Analyzes discontinuities between adjacent packets that could cause clicks/pops.
        This is crucial for OXFW971 devices that sync on DBC.
        """
        samples, packets = self.get_clean_audio_samples(channel_select, trim_edges=True)
        
        if len(packets) < 2:
            return {"error": "Need at least 2 packets for continuity analysis"}
        
        discontinuities = []
        packet_boundaries = []
        sample_idx = 0
        
        for i, packet in enumerate(packets[:-1]):
            next_packet = packets[i + 1]
            
            # Get the last sample of current packet and first sample of next packet
            if len(packet.audio_samples) > 0 and len(next_packet.audio_samples) > 0:
                last_sample = packet.audio_samples[-1]
                first_sample = next_packet.audio_samples[0]
                
                # Calculate the jump between packets
                sample_jump = abs(first_sample - last_sample)
                sample_idx += len(packet.audio_samples)
                
                packet_boundaries.append({
                    "packet_index": i,
                    "sample_position": sample_idx,
                    "time_position": sample_idx / self.sample_rate,
                    "sample_jump": sample_jump,
                    "dbc_current": packet.dbc,
                    "dbc_next": next_packet.dbc,
                    "dbc_status_current": getattr(packet, 'dbc_status', 'unknown'),
                    "dbc_status_next": getattr(next_packet, 'dbc_status', 'unknown'),
                    "is_problematic": sample_jump > 0.1 or getattr(next_packet, 'dbc_status', '') == 'incorrect'
                })
                
                # Flag significant discontinuities
                if sample_jump > 0.1:  # Threshold for audible click
                    discontinuities.append({
                        "type": "large_sample_jump",
                        "packet_index": i,
                        "sample_position": sample_idx,
                        "time_ms": (sample_idx / self.sample_rate) * 1000,
                        "jump_magnitude": sample_jump,
                        "dbc_issue": getattr(next_packet, 'dbc_status', '') == 'incorrect'
                    })
        
        return {
            "total_boundaries": len(packet_boundaries),
            "discontinuities": discontinuities,
            "packet_boundaries": packet_boundaries,
            "max_jump": max([b["sample_jump"] for b in packet_boundaries]) if packet_boundaries else 0,
            "mean_jump": np.mean([b["sample_jump"] for b in packet_boundaries]) if packet_boundaries else 0
        }
    
    def detect_spectral_anomalies(self, samples: np.ndarray, fft_size: int = 8192) -> Dict[str, Any]:
        """
        Detects unusual spectral content that indicates audio anomalies.
        """
        if len(samples) < fft_size:
            fft_size = len(samples)
        
        # Perform FFT
        spectrum = np.abs(rfft(samples[:fft_size]))
        frequencies = rfftfreq(fft_size, 1/self.sample_rate)
        
        # Convert to dB
        spectrum_db = 20 * np.log10(spectrum + 1e-9)
        
        # Define frequency bands for analysis
        nyquist = self.sample_rate / 2
        high_freq_start = nyquist * 0.8  # 80% of Nyquist (17.64 kHz for 44.1kHz)
        ultrasonic_start = nyquist * 0.9   # 90% of Nyquist (19.845 kHz for 44.1kHz)
        
        # Find energy in different bands
        audio_band = frequencies <= high_freq_start
        high_band = (frequencies > high_freq_start) & (frequencies <= ultrasonic_start)
        ultrasonic_band = frequencies > ultrasonic_start
        
        audio_energy = np.mean(spectrum_db[audio_band])
        high_energy = np.mean(spectrum_db[high_band]) if np.any(high_band) else -100
        ultrasonic_energy = np.mean(spectrum_db[ultrasonic_band]) if np.any(ultrasonic_band) else -100
        
        # Detect anomalies
        anomalies = []
        
        # Check for excessive high-frequency content
        if high_energy > (audio_energy - 20):  # High frequencies shouldn't be within 20dB of audio
            anomalies.append({
                "type": "excessive_high_frequency",
                "description": f"High frequency energy too strong: {high_energy:.1f} dB vs {audio_energy:.1f} dB audio",
                "frequency_range": f"{high_freq_start:.0f}-{ultrasonic_start:.0f} Hz",
                "severity": "moderate"
            })
        
        # Check for ultrasonic content (clicks/pops create broadband noise)
        if ultrasonic_energy > (audio_energy - 30):
            anomalies.append({
                "type": "ultrasonic_content",
                "description": f"Ultrasonic energy detected: {ultrasonic_energy:.1f} dB (indicates clicks/pops)",
                "frequency_range": f"{ultrasonic_start:.0f}-{nyquist:.0f} Hz",
                "severity": "high"
            })
        
        # Find spectral peaks in high-frequency region
        high_freq_indices = frequencies > high_freq_start
        high_freq_spectrum = spectrum_db[high_freq_indices]
        high_freq_freqs = frequencies[high_freq_indices]
        
        if len(high_freq_spectrum) > 10:
            # Find peaks in high-frequency region
            peaks, properties = signal.find_peaks(high_freq_spectrum, 
                                                 height=audio_energy - 15,  # Peaks above threshold
                                                 distance=10)  # Minimum separation
            
            if len(peaks) > 0:
                peak_freqs = high_freq_freqs[peaks]
                peak_levels = high_freq_spectrum[peaks]
                
                for freq, level in zip(peak_freqs, peak_levels):
                    anomalies.append({
                        "type": "high_frequency_peak",
                        "description": f"Spectral peak at {freq:.0f} Hz ({level:.1f} dB)",
                        "frequency": freq,
                        "level_db": level,
                        "severity": "high" if freq > ultrasonic_start else "moderate"
                    })
        
        return {
            "fft_size": fft_size,
            "audio_energy_db": audio_energy,
            "high_freq_energy_db": high_energy,
            "ultrasonic_energy_db": ultrasonic_energy,
            "anomalies": anomalies,
            "spectrum_db": spectrum_db,
            "frequencies": frequencies
        }
    
    def correlate_dbc_with_audio_issues(self, channel_select=None) -> Dict[str, Any]:
        """
        Correlates DBC discontinuities with actual audio quality problems.
        Critical for OXFW971 devices that use DBC for synchronization.
        """
        # Get DBC analysis
        dbc_results = self.analyzer.analyze_dbc_continuity()
        
        # Get audio discontinuity analysis
        continuity_results = self.analyze_inter_packet_discontinuities(channel_select)
        
        # Get clean audio samples for spectral analysis
        samples, packets = self.get_clean_audio_samples(channel_select)
        
        if len(samples) == 0:
            return {"error": "No audio samples available"}
        
        spectral_results = self.detect_spectral_anomalies(samples)
        
        # Correlate DBC issues with audio problems
        correlations = []
        
        for dbc_issue in dbc_results.get("discontinuities", []):
            packet_idx = dbc_issue["packet_index"]
            
            # Find corresponding audio discontinuities
            audio_issues = [
                disc for disc in continuity_results.get("discontinuities", [])
                if abs(disc["packet_index"] - packet_idx) <= 1
            ]
            
            correlation = {
                "dbc_issue": dbc_issue,
                "related_audio_issues": audio_issues,
                "has_audio_impact": len(audio_issues) > 0,
                "description": f"DBC issue at packet {packet_idx}"
            }
            
            if audio_issues:
                correlation["description"] += f" correlates with {len(audio_issues)} audio discontinuity(s)"
            
            correlations.append(correlation)
        
        # Calculate correlation statistics
        dbc_issues_with_audio_impact = sum(1 for c in correlations if c["has_audio_impact"])
        total_dbc_issues = len(correlations)
        
        return {
            "dbc_analysis": dbc_results,
            "continuity_analysis": continuity_results,
            "spectral_analysis": spectral_results,
            "correlations": correlations,
            "correlation_stats": {
                "total_dbc_issues": total_dbc_issues,
                "dbc_issues_with_audio_impact": dbc_issues_with_audio_impact,
                "correlation_percentage": (dbc_issues_with_audio_impact / total_dbc_issues * 100) if total_dbc_issues > 0 else 0,
                "total_audio_discontinuities": len(continuity_results.get("discontinuities", [])),
                "total_spectral_anomalies": len(spectral_results.get("anomalies", []))
            }
        }
    
    def detect_clicks_and_pops(self, samples: np.ndarray, threshold: float = 0.1) -> Dict[str, Any]:
        """
        Detects clicks and pops in the audio signal using multiple methods.
        """
        if len(samples) < 3:
            return {"error": "Not enough samples for click detection"}
        
        # Method 1: Large sample-to-sample differences
        diff1 = np.abs(np.diff(samples))
        click_candidates_diff = np.where(diff1 > threshold)[0]
        
        # Method 2: Second derivative (acceleration) method
        diff2 = np.abs(np.diff(samples, n=2))
        click_candidates_diff2 = np.where(diff2 > threshold)[0]
        
        # Method 3: High-frequency energy method
        # Apply high-pass filter to isolate clicks
        if len(samples) > 100:
            sos = signal.butter(4, 8000, btype='high', fs=self.sample_rate, output='sos')
            filtered = signal.sosfilt(sos, samples)
            filtered_energy = np.abs(filtered)
            click_candidates_energy = np.where(filtered_energy > threshold)[0]
        else:
            click_candidates_energy = []
        
        # Combine all methods
        all_candidates = np.unique(np.concatenate([
            click_candidates_diff,
            click_candidates_diff2,
            click_candidates_energy
        ]))
        
        # Convert sample indices to time
        click_times = all_candidates / self.sample_rate
        
        clicks = []
        for i, sample_idx in enumerate(all_candidates):
            click_info = {
                "sample_index": int(sample_idx),
                "time_ms": float(click_times[i] * 1000),
                "amplitude": float(samples[sample_idx]) if sample_idx < len(samples) else 0,
                "detection_methods": []
            }
            
            if sample_idx in click_candidates_diff:
                click_info["detection_methods"].append("large_difference")
            if sample_idx in click_candidates_diff2:
                click_info["detection_methods"].append("second_derivative")
            if sample_idx in click_candidates_energy:
                click_info["detection_methods"].append("high_frequency_energy")
                
            clicks.append(click_info)
        
        return {
            "total_clicks": len(clicks),
            "click_rate_per_second": len(clicks) / (len(samples) / self.sample_rate) if len(samples) > 0 else 0,
            "clicks": clicks,
            "threshold_used": threshold,
            "analysis_duration_ms": len(samples) / self.sample_rate * 1000
        }
    
    def comprehensive_audio_quality_report(self, channel_select=None) -> Dict[str, Any]:
        """
        Generates a comprehensive audio quality analysis report.
        """
        samples, packets = self.get_clean_audio_samples(channel_select)
        
        if len(samples) == 0:
            return {"error": "No audio samples available for analysis"}
        
        # Perform all analyses
        correlation_analysis = self.correlate_dbc_with_audio_issues(channel_select)
        click_analysis = self.detect_clicks_and_pops(samples)
        
        # Calculate overall quality score (0-100)
        quality_score = 100
        issues = []
        
        # Deduct points for various issues
        dbc_issues = len(correlation_analysis.get("dbc_analysis", {}).get("discontinuities", []))
        if dbc_issues > 0:
            quality_score -= min(50, dbc_issues * 5)  # Max 50 points deduction
            issues.append(f"{dbc_issues} DBC discontinuities")
        
        spectral_anomalies = len(correlation_analysis.get("spectral_analysis", {}).get("anomalies", []))
        if spectral_anomalies > 0:
            quality_score -= min(30, spectral_anomalies * 10)  # Max 30 points deduction
            issues.append(f"{spectral_anomalies} spectral anomalies")
        
        clicks = click_analysis.get("total_clicks", 0)
        if clicks > 0:
            quality_score -= min(20, clicks * 2)  # Max 20 points deduction
            issues.append(f"{clicks} clicks/pops detected")
        
        quality_score = max(0, quality_score)  # Don't go below 0
        
        return {
            "overall_quality_score": quality_score,
            "quality_grade": "Excellent" if quality_score >= 90 else 
                           "Good" if quality_score >= 70 else
                           "Fair" if quality_score >= 50 else "Poor",
            "issues_summary": issues,
            "detailed_analysis": {
                "correlation_analysis": correlation_analysis,
                "click_analysis": click_analysis
            },
            "recommendations": self._generate_recommendations(correlation_analysis, click_analysis)
        }
    
    def _generate_recommendations(self, correlation_analysis: Dict, click_analysis: Dict) -> List[str]:
        """
        Generates recommendations based on the analysis results.
        """
        recommendations = []
        
        # DBC-related recommendations
        dbc_issues = len(correlation_analysis.get("dbc_analysis", {}).get("discontinuities", []))
        if dbc_issues > 0:
            recommendations.append(
                "DBC discontinuities detected. For OXFW971 devices, this directly affects audio quality. "
                "Check FireWire cable quality, reduce bus activity, or try a different FireWire port."
            )
        
        # Spectral anomaly recommendations
        spectral_anomalies = correlation_analysis.get("spectral_analysis", {}).get("anomalies", [])
        high_freq_issues = [a for a in spectral_anomalies if "high_frequency" in a.get("type", "")]
        if high_freq_issues:
            recommendations.append(
                "High-frequency spectral content detected, indicating clicks/pops or reconstruction errors. "
                "This correlates with the DBC issues - improving FireWire stability should help."
            )
        
        # Click detection recommendations
        clicks = click_analysis.get("total_clicks", 0)
        if clicks > 0:
            recommendations.append(
                f"{clicks} clicks/pops detected in audio. These may be caused by the DBC synchronization "
                "issues. Try using a higher-quality FireWire cable or reducing system load."
            )
        
        # Correlation-specific recommendations
        correlation_stats = correlation_analysis.get("correlation_stats", {})
        correlation_pct = correlation_stats.get("correlation_percentage", 0)
        if correlation_pct > 50:
            recommendations.append(
                f"{correlation_pct:.0f}% of DBC issues correlate with audio problems. "
                "This confirms that DBC stability is critical for your device."
            )
        
        if not recommendations:
            recommendations.append("Audio quality appears good with no major issues detected.")
        
        return recommendations