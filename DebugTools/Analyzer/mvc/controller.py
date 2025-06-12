# mvc/controller.py
import streamlit as st
import numpy as np
from typing import List, Optional
from firewire.log_parser import parse_log_content
from firewire.analyzer import Analyzer
from firewire.cip_packet import CIPPacket


class AppController:
    """
    Controller class that handles the main application logic and coordinates
    between the model (Analyzer) and the view components.
    """
    
    def __init__(self):
        self.analyzer: Optional[Analyzer] = None
        self.packets: List[CIPPacket] = []
        
    def load_files(self, uploaded_files) -> bool:
        """
        Loads and parses uploaded files.
        Returns True if successful, False otherwise.
        """
        if not uploaded_files:
            return False
            
        try:
            all_content = "".join([
                file.read().decode('utf-8', errors='ignore') 
                for file in uploaded_files
            ])
            
            self.packets = parse_log_content(all_content)
            if not self.packets:
                st.error("No valid FireWire packets could be parsed from the uploaded files.")
                return False
                
            self.analyzer = Analyzer(self.packets)
            st.success(f"Successfully parsed {len(self.packets)} total packets from {len(uploaded_files)} file(s).")
            return True
            
        except Exception as e:
            st.error(f"Error parsing files: {str(e)}")
            return False
    
    def get_format_info(self) -> dict:
        """Returns stream format information."""
        if not self.analyzer:
            return {"error": "No analyzer available"}
        return self.analyzer.format_info
    
    def get_packet_type_analysis(self) -> dict:
        """Returns packet type distribution analysis."""
        if not self.analyzer:
            return {"error": "No analyzer available"}
        return self.analyzer.analyze_packet_types()
    
    def get_dbc_analysis(self) -> dict:
        """Returns DBC continuity analysis."""
        if not self.analyzer:
            return {"error": "No analyzer available"}
        return self.analyzer.analyze_dbc_continuity()
    
    def get_syt_analysis(self) -> dict:
        """Returns SYT timestamp analysis."""
        if not self.analyzer:
            return {"error": "No analyzer available"}
        return self.analyzer.analyze_syt_timestamp()
    
    def get_audio_samples(self, channel_select=None) -> np.ndarray:
        """Returns aggregated audio samples for the specified channel."""
        if not self.analyzer:
            return []
        return self.analyzer.get_aggregated_audio_samples(channel_select)
    
    def get_audio_analysis(self, samples) -> dict:
        """Returns audio waveform analysis."""
        if not self.analyzer:
            return {}
        return self.analyzer.analyze_audio_waveform(samples)
    
    def get_unique_channels(self) -> List[int]:
        """Returns list of unique audio channels."""
        if not self.analyzer:
            return []
        return sorted(list(set(p.channel for p in self.analyzer.data_packets)))
    
    def get_sample_rate(self) -> int:
        """Returns the sample rate."""
        if not self.analyzer:
            return 44100
        return self.analyzer.sample_rate
    
    def filter_packets(self, channel_filter="All", packet_type_filter="All", max_packets=50) -> List[CIPPacket]:
        """
        Filters packets based on specified criteria.
        """
        if not self.analyzer:
            return []
            
        filtered_packets = self.analyzer.all_packets
        
        if channel_filter != "All":
            filtered_packets = [p for p in filtered_packets if p.channel == channel_filter]
            
        if packet_type_filter == "Data Packets Only":
            filtered_packets = [p for p in filtered_packets if p.is_data_packet]
        elif packet_type_filter == "Non-Data Packets Only":
            filtered_packets = [p for p in filtered_packets if not p.is_data_packet]
        
        return filtered_packets[:max_packets]
    
    def get_packet_pattern(self, num_packets=50) -> str:
        """
        Returns a string representation of the data/no-data pattern for the first N packets.
        D = Data packet, N = No-data packet
        """
        if not self.analyzer:
            return ""
        
        pattern_analysis = self.analyzer.get_packet_pattern_analysis(num_packets)
        return pattern_analysis["pattern"]
    
    def get_packet_pattern_analysis(self, num_packets=50) -> dict:
        """
        Returns detailed packet pattern analysis.
        """
        if not self.analyzer:
            return {}
        
        return self.analyzer.get_packet_pattern_analysis(num_packets)
    
    def calculate_packet_statistics(self, filtered_packets: List[CIPPacket]) -> dict:
        """
        Calculates statistics for the filtered packet list.
        """
        if not filtered_packets:
            return {
                "total_filtered": 0,
                "data_packets": 0,
                "zero_sample_packets": 0,
                "problem_packets": 0,
                "zero_sample_percent": 0
            }
            
        total_data_packets = sum(1 for p in filtered_packets if p.is_data_packet)
        total_zero_sample_packets = sum(1 for p in filtered_packets if getattr(p, 'samples_are_zero', False))
        
        # Count problematic packets
        problematic_packets = 0
        for p in filtered_packets:
            dbc_status = getattr(p, 'dbc_status', None)
            if dbc_status == "incorrect":
                problematic_packets += 1
            elif not p.is_data_packet and hasattr(p, 'no_data_issues'):
                problematic_packets += 1
            elif p.is_data_packet and getattr(p, 'samples_are_zero', False):
                problematic_packets += 1
        
        return {
            "total_filtered": len(filtered_packets),
            "data_packets": total_data_packets,
            "zero_sample_packets": total_zero_sample_packets,
            "problem_packets": problematic_packets,
            "problem_percent": (problematic_packets/len(filtered_packets)*100) if len(filtered_packets) > 0 else 0,
            "zero_sample_percent": (total_zero_sample_packets/total_data_packets)*100 if total_data_packets > 0 else 0
        }
