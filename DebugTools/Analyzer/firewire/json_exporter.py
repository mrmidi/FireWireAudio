# firewire/json_exporter.py
import json
import numpy as np
from datetime import datetime
from typing import Any, Dict, List

class AnalysisJSONExporter:
    """
    Exports analysis results to JSON format for sharing and debugging.
    Handles numpy array serialization and creates comprehensive reports.
    """
    
    @staticmethod
    def serialize_numpy(obj):
        """Convert numpy objects to JSON-serializable types."""
        if isinstance(obj, np.ndarray):
            return obj.tolist()
        elif isinstance(obj, np.integer):
            return int(obj)
        elif isinstance(obj, np.floating):
            return float(obj)
        elif isinstance(obj, np.bool_):
            return bool(obj)
        elif isinstance(obj, dict):
            return {key: AnalysisJSONExporter.serialize_numpy(value) for key, value in obj.items()}
        elif isinstance(obj, list):
            return [AnalysisJSONExporter.serialize_numpy(item) for item in obj]
        else:
            return obj
    
    @staticmethod
    def export_comprehensive_analysis(controller, channel_select=None, include_raw_data=False) -> Dict[str, Any]:
        """
        Exports comprehensive analysis results to a JSON-serializable dictionary.
        """
        export_data = {
            "export_info": {
                "timestamp": datetime.now().isoformat(),
                "analyzer_version": "1.0.0",
                "channel_analyzed": channel_select,
                "include_raw_data": include_raw_data
            }
        }
        
        try:
            # Basic stream information
            format_info = controller.get_format_info()
            export_data["stream_format"] = AnalysisJSONExporter.serialize_numpy(format_info)
            
            # Packet statistics
            packet_stats = controller.get_packet_type_analysis()
            export_data["packet_statistics"] = AnalysisJSONExporter.serialize_numpy(packet_stats)
            
            # DBC analysis
            dbc_analysis = controller.get_dbc_analysis()
            export_data["dbc_analysis"] = AnalysisJSONExporter.serialize_numpy(dbc_analysis)
            
            # SYT analysis
            syt_analysis = controller.get_syt_analysis()
            export_data["syt_analysis"] = AnalysisJSONExporter.serialize_numpy(syt_analysis)
            
            # Audio samples info
            unique_channels = controller.get_unique_channels()
            export_data["available_channels"] = unique_channels
            
            if unique_channels and controller.anomaly_analyzer:
                # Get samples for analysis
                samples = controller.get_clean_audio_samples(channel_select, trim_edges=True)
                
                if len(samples) > 0:
                    # Basic audio metrics
                    audio_analysis = controller.get_audio_analysis(samples)
                    export_data["audio_metrics"] = AnalysisJSONExporter.serialize_numpy(audio_analysis)
                    
                    # Comprehensive quality report
                    quality_report = controller.get_comprehensive_audio_quality_report(channel_select)
                    export_data["quality_report"] = AnalysisJSONExporter.serialize_numpy(quality_report)
                    
                    # Individual analyses
                    click_analysis = controller.get_click_analysis(channel_select, threshold=0.1)
                    export_data["click_analysis"] = AnalysisJSONExporter.serialize_numpy(click_analysis)
                    
                    spectral_analysis = controller.get_spectral_anomaly_analysis(channel_select, fft_size=8192)
                    export_data["spectral_analysis"] = AnalysisJSONExporter.serialize_numpy(spectral_analysis)
                    
                    dbc_correlation = controller.get_dbc_audio_correlation(channel_select)
                    export_data["dbc_correlation"] = AnalysisJSONExporter.serialize_numpy(dbc_correlation)
                    
                    # Raw audio samples (optional, can be large)
                    if include_raw_data:
                        # Limit to first 10000 samples to avoid huge files
                        sample_limit = min(10000, len(samples))
                        export_data["raw_audio_samples"] = {
                            "samples": samples[:sample_limit].tolist(),
                            "sample_rate": controller.get_sample_rate(),
                            "total_samples": len(samples),
                            "included_samples": sample_limit,
                            "duration_seconds": len(samples) / controller.get_sample_rate()
                        }
            
            # Packet details (summary only to avoid huge files)
            if controller.analyzer:
                all_packets = controller.analyzer.all_packets
                packet_summary = []
                
                for i, packet in enumerate(all_packets[:100]):  # First 100 packets
                    if packet.is_valid:
                        packet_info = {
                            "index": i,
                            "channel": packet.channel,
                            "dbc": packet.dbc,
                            "syt": packet.syt,
                            "fdf": packet.fdf,
                            "is_data_packet": packet.is_data_packet,
                            "timestamp": packet.get_timestamp_string() if hasattr(packet, 'get_timestamp_string') else None,
                            "sample_count": len(packet.audio_samples) if hasattr(packet, 'audio_samples') else 0,
                            "samples_are_zero": getattr(packet, 'samples_are_zero', False),
                            "dbc_status": getattr(packet, 'dbc_status', 'unknown')
                        }
                        packet_summary.append(packet_info)
                
                export_data["packet_summary"] = packet_summary
                export_data["total_packets"] = len(all_packets)
            
            # System information
            export_data["system_info"] = {
                "total_packets_analyzed": len(controller.packets) if controller.packets else 0,
                "analyzer_sample_rate": controller.get_sample_rate(),
                "analysis_successful": True
            }
            
        except Exception as e:
            export_data["error"] = {
                "message": str(e),
                "type": type(e).__name__,
                "analysis_failed": True
            }
        
        return export_data
    
    @staticmethod
    def export_to_json_string(controller, channel_select=None, include_raw_data=False, indent=2) -> str:
        """
        Exports analysis to JSON string format.
        """
        data = AnalysisJSONExporter.export_comprehensive_analysis(
            controller, channel_select, include_raw_data
        )
        return json.dumps(data, indent=indent, ensure_ascii=False)
    
    @staticmethod
    def export_lightweight_summary(controller, channel_select=None) -> Dict[str, Any]:
        """
        Exports a lightweight summary for quick sharing (no raw data).
        """
        export_data = {
            "summary_info": {
                "timestamp": datetime.now().isoformat(),
                "channel_analyzed": channel_select,
                "export_type": "lightweight_summary"
            }
        }
        
        try:
            # Key metrics only
            format_info = controller.get_format_info()
            export_data["sample_rate"] = format_info.get("nominal_sample_rate", "unknown")
            export_data["syt_interval"] = format_info.get("syt_interval", "unknown")
            
            # DBC issues summary
            dbc_analysis = controller.get_dbc_analysis()
            export_data["dbc_issues"] = {
                "total_discontinuities": len(dbc_analysis.get("discontinuities", [])),
                "channels_affected": dbc_analysis.get("channels_found", []),
                "expected_increment": dbc_analysis.get("expected_increment", "unknown")
            }
            
            # Quality summary
            if controller.anomaly_analyzer and controller.get_unique_channels():
                quality_report = controller.get_comprehensive_audio_quality_report(channel_select)
                if "error" not in quality_report:
                    export_data["quality_summary"] = {
                        "overall_score": quality_report.get("overall_quality_score", 0),
                        "quality_grade": quality_report.get("quality_grade", "unknown"),
                        "issues_found": quality_report.get("issues_summary", []),
                        "recommendations": quality_report.get("recommendations", [])
                    }
                    
                    # Key anomaly counts
                    detailed = quality_report.get("detailed_analysis", {})
                    if "correlation_analysis" in detailed:
                        corr_stats = detailed["correlation_analysis"].get("correlation_stats", {})
                        export_data["anomaly_summary"] = {
                            "total_dbc_issues": corr_stats.get("total_dbc_issues", 0),
                            "dbc_with_audio_impact": corr_stats.get("dbc_issues_with_audio_impact", 0),
                            "correlation_percentage": corr_stats.get("correlation_percentage", 0),
                            "spectral_anomalies": corr_stats.get("total_spectral_anomalies", 0),
                            "audio_discontinuities": corr_stats.get("total_audio_discontinuities", 0)
                        }
                        
                        # Spectral anomaly details
                        spectral_analysis = detailed["correlation_analysis"].get("spectral_analysis", {})
                        if spectral_analysis and "anomalies" in spectral_analysis:
                            export_data["spectral_issues"] = []
                            for anomaly in spectral_analysis["anomalies"]:
                                export_data["spectral_issues"].append({
                                    "type": anomaly.get("type", "unknown"),
                                    "description": anomaly.get("description", ""),
                                    "severity": anomaly.get("severity", "unknown"),
                                    "frequency": anomaly.get("frequency", None)
                                })
                    
                    # Click summary
                    if "click_analysis" in detailed:
                        click_data = detailed["click_analysis"]
                        export_data["click_summary"] = {
                            "total_clicks": click_data.get("total_clicks", 0),
                            "click_rate_per_second": click_data.get("click_rate_per_second", 0),
                            "analysis_duration_ms": click_data.get("analysis_duration_ms", 0)
                        }
            
        except Exception as e:
            export_data["error"] = str(e)
        
        return export_data