# firewire/packet_anomaly_analyzer.py
import re
import numpy as np
from typing import List, Dict, Any, Tuple
from .cip_packet import CIPPacket

class PacketAnomalyAnalyzer:
    """
    Advanced packet anomaly detection for FireWire audio streams.
    Detects length errors, dropouts, DBC issues, and other packet-level anomalies.
    """
    
    def __init__(self, analyzer):
        self.analyzer = analyzer
        self.sample_rate = analyzer.sample_rate
        
    def detect_length_errors(self) -> Dict[str, Any]:
        """
        Detects LENGTH ERROR anomalies from the raw log data.
        These occur when actual packet size doesn't match declared size.
        """
        length_errors = []
        
        for packet in self.analyzer.all_packets:
            raw_dict = getattr(packet, 'raw_packet_dict', {})
            
            # Check for explicit LENGTH ERROR flag from parser
            has_explicit_error = getattr(packet, 'has_length_error', False)
            length_error_bytes = getattr(packet, 'length_error_bytes', 0)
            
            # Check for size mismatch
            declared_size = raw_dict.get('size', 0)
            actual_size = raw_dict.get('actual_size', 0)
            
            if has_explicit_error or declared_size != actual_size:
                size_diff = actual_size - declared_size
                
                # Determine severity
                if has_explicit_error or abs(size_diff) > 100:
                    severity = "high"
                elif abs(size_diff) > 20:
                    severity = "moderate"
                else:
                    severity = "low"
                
                error_info = {
                    "packet_index": getattr(packet, 'original_index', -1),
                    "channel": packet.channel,
                    "timestamp": packet.get_timestamp_string(),
                    "declared_size": declared_size,
                    "actual_size": actual_size,
                    "size_diff": size_diff,
                    "severity": severity,
                    "has_explicit_length_error": has_explicit_error,
                    "length_error_bytes": length_error_bytes,
                    "hex_data_truncated": packet.hex_data[:50] + "..." if len(packet.hex_data) > 50 else packet.hex_data
                }
                
                length_errors.append(error_info)
        
        return {
            "total_errors": len(length_errors),
            "high_severity": len([e for e in length_errors if e["severity"] == "high"]),
            "moderate_severity": len([e for e in length_errors if e["severity"] == "moderate"]),
            "low_severity": len([e for e in length_errors if e["severity"] == "low"]),
            "explicit_length_errors": len([e for e in length_errors if e["has_explicit_length_error"]]),
            "errors": length_errors
        }
    
    def detect_audio_dropouts(self, channel_select=None) -> Dict[str, Any]:
        """
        Detects audio dropout patterns where packets contain all zeros or repeated patterns.
        """
        data_packets = [p for p in self.analyzer.all_packets if p.is_valid and p.is_data_packet]
        
        if channel_select is not None:
            data_packets = [p for p in data_packets if p.channel == channel_select]
        
        dropouts = []
        dropout_regions = []
        
        # Track consecutive zero/silent packets
        current_dropout = None
        
        for i, packet in enumerate(data_packets):
            is_dropout = False
            dropout_type = None
            
            # Check for all-zero samples
            if hasattr(packet, 'samples_are_zero') and packet.samples_are_zero:
                is_dropout = True
                dropout_type = "all_zeros"
            
            # Check for extremely low amplitude (near silence)
            elif len(packet.audio_samples) > 0:
                max_amplitude = np.max(np.abs(packet.audio_samples))
                if max_amplitude < 0.001:  # Very quiet threshold
                    is_dropout = True
                    dropout_type = "near_silence"
            
            # Check for repeated patterns
            if len(packet.audio_samples) > 4:
                # Simple pattern detection - check if all samples are identical
                if np.all(packet.audio_samples == packet.audio_samples[0]):
                    is_dropout = True
                    dropout_type = "repeated_pattern"
            
            if is_dropout:
                dropout_info = {
                    "packet_index": getattr(packet, 'original_index', i),
                    "channel": packet.channel,
                    "timestamp": packet.get_timestamp_string(),
                    "dbc": packet.dbc,
                    "type": dropout_type,
                    "max_amplitude": np.max(np.abs(packet.audio_samples)) if len(packet.audio_samples) > 0 else 0,
                    "sample_count": len(packet.audio_samples),
                    "hex_samples": packet.audio_samples_hex[:3] if hasattr(packet, 'audio_samples_hex') else []
                }
                dropouts.append(dropout_info)
                
                # Track consecutive dropouts as regions
                if current_dropout is None:
                    current_dropout = {
                        "start_packet": dropout_info["packet_index"],
                        "start_timestamp": dropout_info["timestamp"],
                        "end_packet": dropout_info["packet_index"],
                        "end_timestamp": dropout_info["timestamp"],
                        "packet_count": 1,
                        "channel": dropout_info["channel"],
                        "types": [dropout_type]
                    }
                else:
                    # Extend current dropout region
                    current_dropout["end_packet"] = dropout_info["packet_index"]
                    current_dropout["end_timestamp"] = dropout_info["timestamp"]
                    current_dropout["packet_count"] += 1
                    if dropout_type not in current_dropout["types"]:
                        current_dropout["types"].append(dropout_type)
            else:
                # End of dropout region
                if current_dropout is not None:
                    dropout_regions.append(current_dropout)
                    current_dropout = None
        
        # Don't forget the last dropout region
        if current_dropout is not None:
            dropout_regions.append(current_dropout)
        
        return {
            "total_dropout_packets": len(dropouts),
            "dropout_regions": len(dropout_regions),
            "dropouts": dropouts,
            "regions": dropout_regions,
            "channels_affected": list(set([d["channel"] for d in dropouts]))
        }
    
    def detect_repeated_patterns(self, channel_select=None) -> Dict[str, Any]:
        """
        Detects suspicious repeated patterns in audio data that might indicate device malfunction.
        """
        data_packets = [p for p in self.analyzer.all_packets if p.is_valid and p.is_data_packet]
        
        if channel_select is not None:
            data_packets = [p for p in data_packets if p.channel == channel_select]
        
        pattern_anomalies = []
        
        for i, packet in enumerate(data_packets):
            if len(packet.audio_samples) < 4:
                continue
                
            samples = packet.audio_samples
            
            # Check for exact repetition of values
            unique_values = len(np.unique(samples))
            if unique_values == 1 and samples[0] != 0:
                pattern_anomalies.append({
                    "packet_index": getattr(packet, 'original_index', i),
                    "channel": packet.channel,
                    "timestamp": packet.get_timestamp_string(),
                    "type": "single_value_repetition",
                    "value": float(samples[0]),
                    "sample_count": len(samples)
                })
            
            # Check for simple alternating patterns
            elif len(samples) >= 8:
                # Check if pattern repeats every 2 samples
                is_alternating = True
                for j in range(2, len(samples)):
                    if abs(samples[j] - samples[j % 2]) > 1e-6:
                        is_alternating = False
                        break
                
                if is_alternating and unique_values <= 2:
                    pattern_anomalies.append({
                        "packet_index": getattr(packet, 'original_index', i),
                        "channel": packet.channel,
                        "timestamp": packet.get_timestamp_string(),
                        "type": "alternating_pattern",
                        "pattern_values": [float(samples[0]), float(samples[1])],
                        "sample_count": len(samples)
                    })
        
        return {
            "total_pattern_anomalies": len(pattern_anomalies),
            "anomalies": pattern_anomalies,
            "channels_affected": list(set([a["channel"] for a in pattern_anomalies]))
        }
    
    def comprehensive_packet_analysis(self, channel_select=None) -> Dict[str, Any]:
        """
        Performs comprehensive packet-level anomaly analysis.
        """
        # Get DBC analysis
        dbc_results = self.analyzer.analyze_dbc_continuity()
        
        # Detect length errors
        length_errors = self.detect_length_errors()
        
        # Detect audio dropouts
        dropout_analysis = self.detect_audio_dropouts(channel_select)
        
        # Detect repeated patterns
        pattern_analysis = self.detect_repeated_patterns(channel_select)
        
        # Calculate overall packet health score
        total_packets = len([p for p in self.analyzer.all_packets if p.is_valid])
        
        issues_count = (
            len(dbc_results.get("discontinuities", [])) +
            length_errors.get("total_errors", 0) +
            dropout_analysis.get("total_dropout_packets", 0) +
            pattern_analysis.get("total_pattern_anomalies", 0)
        )
        
        health_score = max(0, 100 - (issues_count / max(total_packets, 1) * 100))
        
        # Determine severity level
        if health_score >= 95:
            severity = "excellent"
        elif health_score >= 85:
            severity = "good"
        elif health_score >= 70:
            severity = "moderate"
        elif health_score >= 50:
            severity = "poor"
        else:
            severity = "critical"
        
        # Generate recommendations
        recommendations = self._generate_packet_recommendations(
            dbc_results, length_errors, dropout_analysis, pattern_analysis
        )
        
        return {
            "analysis_timestamp": self._get_timestamp(),
            "channel_analyzed": channel_select,
            "packet_health_score": round(health_score, 1),
            "severity_level": severity,
            "total_packets_analyzed": total_packets,
            "total_issues": issues_count,
            
            # Detailed analyses
            "dbc_analysis": dbc_results,
            "length_errors": length_errors,
            "dropout_analysis": dropout_analysis,
            "pattern_analysis": pattern_analysis,
            
            # Summary
            "issue_summary": {
                "dbc_discontinuities": len(dbc_results.get("discontinuities", [])),
                "length_errors": length_errors.get("total_errors", 0),
                "dropout_packets": dropout_analysis.get("total_dropout_packets", 0),
                "pattern_anomalies": pattern_analysis.get("total_pattern_anomalies", 0)
            },
            
            "recommendations": recommendations
        }
    
    def _generate_packet_recommendations(self, dbc_results, length_errors, dropout_analysis, pattern_analysis) -> List[str]:
        """Generate specific recommendations based on detected anomalies."""
        recommendations = []
        
        # DBC issues
        if len(dbc_results.get("discontinuities", [])) > 0:
            recommendations.append(
                "🔄 DBC discontinuities detected - Check device synchronization and consider buffer adjustments"
            )
        
        # Length errors
        if length_errors.get("total_errors", 0) > 0:
            recommendations.append(
                "📏 Packet length errors detected - Investigate USB/FireWire connection stability"
            )
            if length_errors.get("high_severity", 0) > 0:
                recommendations.append(
                    "⚠️ Severe length errors present - Check cable integrity and power supply"
                )
        
        # Dropouts
        if dropout_analysis.get("total_dropout_packets", 0) > 0:
            recommendations.append(
                "🔇 Audio dropouts detected - Check for CPU overload or buffer underruns"
            )
            if dropout_analysis.get("dropout_regions", 0) > 3:
                recommendations.append(
                    "📊 Multiple dropout regions found - Consider increasing buffer sizes"
                )
        
        # Pattern anomalies
        if pattern_analysis.get("total_pattern_anomalies", 0) > 0:
            recommendations.append(
                "🔁 Suspicious audio patterns detected - Check device firmware and driver compatibility"
            )
        
        if not recommendations:
            recommendations.append("✅ No significant packet-level anomalies detected - Stream appears healthy")
        
        return recommendations
    
    def _get_timestamp(self) -> str:
        """Get current timestamp for reporting."""
        import datetime
        return datetime.datetime.now().isoformat()
    
    def export_packet_samples(self, max_samples_per_type: int = 5) -> Dict[str, Any]:
        """
        Export sample packets for each type of anomaly for debugging/reporting.
        """
        # Get comprehensive analysis
        analysis = self.comprehensive_packet_analysis()
        
        samples = {
            "dbc_discontinuity_samples": [],
            "length_error_samples": [],
            "dropout_samples": [],
            "pattern_anomaly_samples": []
        }
        
        # Sample DBC discontinuities
        dbc_issues = analysis["dbc_analysis"].get("discontinuities", [])
        for issue in dbc_issues[:max_samples_per_type]:
            packet_idx = issue.get("packet_index", -1)
            if packet_idx >= 0 and packet_idx < len(self.analyzer.all_packets):
                packet = self.analyzer.all_packets[packet_idx]
                samples["dbc_discontinuity_samples"].append(self._serialize_packet_sample(packet, issue))
        
        # Sample length errors
        length_issues = analysis["length_errors"].get("errors", [])
        for issue in length_issues[:max_samples_per_type]:
            packet_idx = issue.get("packet_index", -1)
            if packet_idx >= 0 and packet_idx < len(self.analyzer.all_packets):
                packet = self.analyzer.all_packets[packet_idx]
                samples["length_error_samples"].append(self._serialize_packet_sample(packet, issue))
        
        # Sample dropouts
        dropout_issues = analysis["dropout_analysis"].get("dropouts", [])
        for issue in dropout_issues[:max_samples_per_type]:
            packet_idx = issue.get("packet_index", -1)
            if packet_idx >= 0 and packet_idx < len(self.analyzer.all_packets):
                packet = self.analyzer.all_packets[packet_idx]
                samples["dropout_samples"].append(self._serialize_packet_sample(packet, issue))
        
        # Sample pattern anomalies
        pattern_issues = analysis["pattern_analysis"].get("anomalies", [])
        for issue in pattern_issues[:max_samples_per_type]:
            packet_idx = issue.get("packet_index", -1)
            if packet_idx >= 0 and packet_idx < len(self.analyzer.all_packets):
                packet = self.analyzer.all_packets[packet_idx]
                samples["pattern_anomaly_samples"].append(self._serialize_packet_sample(packet, issue))
        
        return samples
    
    def _serialize_packet_sample(self, packet: CIPPacket, issue_info: Dict[str, Any]) -> Dict[str, Any]:
        """Serialize a packet with its issue information for export."""
        return {
            "issue_info": issue_info,
            "packet_data": {
                "channel": packet.channel,
                "timestamp": packet.get_timestamp_string(),
                "dbc": packet.dbc,
                "syt": packet.syt,
                "fmt": packet.fmt,
                "fdf": packet.fdf,
                "is_data_packet": packet.is_data_packet,
                "hex_data": packet.hex_data,
                "audio_samples": packet.audio_samples.tolist() if len(packet.audio_samples) > 0 else [],
                "audio_samples_hex": getattr(packet, 'audio_samples_hex', []),
                "raw_packet_info": {
                    "size": packet.raw_packet_dict.get('size', 0),
                    "actual_size": packet.raw_packet_dict.get('actual_size', 0),
                    "tag": packet.tag,
                    "sy": packet.sy
                }
            }
        }
