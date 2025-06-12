# mvc/views.py
import streamlit as st
import numpy as np
import plotly.graph_objects as go
from scipy.fft import rfft, rfftfreq
from typing import List, Dict, Any
from firewire.cip_packet import CIPPacket


class CIPAnalysisView:
    """View component for CIP packet analysis tab."""
    
    @staticmethod
    def render_format_info(fmt_results: Dict[str, Any]):
        """Renders stream format information."""
        st.header("Stream Format (from first data packet)")
        
        if "error" in fmt_results:
            st.warning(fmt_results["error"])
        else:
            col1, col2, col3, col4 = st.columns(4)
            col1.metric("Sample Rate", f"{fmt_results['nominal_sample_rate']/1000:.1f} kHz")
            col2.metric("Subformat Type", fmt_results['subformat_type'])
            col3.metric("SYT Interval", str(fmt_results['syt_interval']), 
                       help="Number of audio frames per isochronous packet.")
            col4.metric("FDF/SFC Code", f"{fmt_results['fdf_full_hex']} / {fmt_results['sfc_code']}", 
                       help="Format-Dependent Field / Sampling Frequency Code")
    
    @staticmethod
    def render_packet_types(packet_type_results: Dict[str, Any]):
        """Renders packet type distribution."""
        st.subheader("Packet Type Distribution")
        
        col1, col2, col3, col4 = st.columns(4)
        col1.metric("Data Packets", packet_type_results['data_packets'], 
                    delta=f"{packet_type_results['data_rate_percent']:.1f}%")
        col2.metric("No-Data Packets", packet_type_results['no_data_packets'], 
                    delta=f"{packet_type_results['no_data_rate_percent']:.1f}%")
        col3.metric("Valid No-Data", packet_type_results['valid_no_data_packets'])
        col4.metric("Invalid No-Data", packet_type_results['invalid_no_data_packets'])
        
        # Show example of valid no-data packet if available
        if packet_type_results['valid_no_data_examples']:
            with st.expander("📋 Example of Valid No-Data Packet"):
                example = packet_type_results['valid_no_data_examples'][0]
                col1, col2, col3, col4, col5 = st.columns(5)
                col1.write(f"**Channel:** {example.channel}")
                col2.write(f"**DBC:** 0x{example.dbc:02X}")
                col3.write(f"**SYT:** 0x{example.syt:04X}")
                col4.write(f"**FDF:** 0x{example.fdf:02X}")
                col5.write(f"**SFC:** {example.fdf & 0b00000111}")
                st.code(f"Raw Hex: {example.hex_data[:50]}{'...' if len(example.hex_data) > 50 else ''}")
                st.caption("✅ Valid no-data packet: SYT=0xFFFF, proper SFC code in FDF")
    
    @staticmethod
    def render_dbc_analysis(dbc_results: Dict[str, Any]):
        """Renders DBC continuity analysis."""
        st.subheader("Data Block Counter (DBC) Continuity")
        
        if not dbc_results["discontinuities"]:
            st.success(
                f"✅ DBC is continuous for all detected channels: {dbc_results['channels_found']}. "
                f"Expected increment is {dbc_results['expected_increment']}."
            )
        else:
            st.error(f"🚨 Found {len(dbc_results['discontinuities'])} DBC discontinuities (potential packet drops).")
            
            # Categorize discontinuities
            data_issues = [d for d in dbc_results["discontinuities"] if d["packet_type"] == "data"]
            no_data_issues = [d for d in dbc_results["discontinuities"] if d["packet_type"] == "no-data"]
            
            with st.expander("Show Discontinuity Details"):
                if data_issues:
                    st.subheader("Data Packet DBC Issues")
                    st.write("**Issues with data packet DBC increments:**")
                    for issue in data_issues:
                        if issue.get("after_no_data"):
                            st.write(f"📦 Packet {issue['packet_index']} (Channel {issue['channel']}): {issue.get('description', 'Data packet after no-data has wrong DBC')}")
                        else:
                            st.write(f"📦 Packet {issue['packet_index']} (Channel {issue['channel']}): {issue.get('description', 'Data packet has wrong DBC increment')}")
                
                if no_data_issues:
                    st.subheader("No-Data Packet DBC Issues")
                    st.write("**Issues with no-data packet DBC increments:**")
                    for issue in no_data_issues:
                        st.write(f"📭 Packet {issue['packet_index']} (Channel {issue['channel']}): {issue.get('description', 'No-data packet has wrong DBC')}")
                
                # Show raw data table
                if st.checkbox("Show Raw Discontinuity Data"):
                    st.dataframe(dbc_results["discontinuities"])
    
    @staticmethod
    def render_syt_analysis(syt_results: Dict[str, Any]):
        """Renders SYT timestamp analysis."""
        st.subheader("Synchronization Timestamp (SYT) Analysis")
        
        if "error" in syt_results:
            st.warning(syt_results["error"])
        else:
            col1, col2, col3, col4 = st.columns(4)
            col1.metric("Mean SYT Delta", f"{syt_results['mean_syt_delta']:.2f}")
            col2.metric("Std Dev (Jitter)", f"{syt_results['std_dev_syt_delta (jitter)']:.2f}")
            col3.metric("Min Delta", f"{syt_results['min_syt_delta']}")
            col4.metric("Max Delta", f"{syt_results['max_syt_delta']}")
            st.caption(f"Theoretical SYT delta per packet: {syt_results['theoretical_syt_delta_per_packet']}")


class AudioAnalysisView:
    """View component for audio waveform analysis tab."""
    
    @staticmethod
    def render_audio_controls(unique_channels: List[int]):
        """Renders audio analysis controls and returns selected values."""
        col1, col2 = st.columns(2)
        channel_select = col1.selectbox("Select Audio Channel", options=unique_channels, index=0)
        time_range = col2.slider("Time Range to Display (seconds)", 0.1, 5.0, 1.0, 0.1)
        return channel_select, time_range
    
    @staticmethod
    def render_audio_metrics(audio_results: Dict[str, Any]):
        """Renders audio waveform metrics."""
        st.subheader("Waveform Metrics")
        
        st.markdown("##### Level Metrics")
        col1, col2, col3 = st.columns(3)
        col1.metric(
            label="Peak Level",
            value=f"{audio_results['peak_dbfs']:.2f} dBFS",
            help="The absolute maximum level of the signal. 0 dBFS is the maximum possible digital level."
        )
        col2.metric(
            label="RMS Level (Loudness)",
            value=f"{audio_results['rms_dbfs']:.2f} dBFS",
            help="Root Mean Square level, a measure of the average power or perceived loudness."
        )
        col3.metric(
            label="Crest Factor",
            value=f"{audio_results['crest_factor']:.2f}",
            help="The ratio of peak level to RMS level. Higher values indicate a more dynamic signal."
        )
        
        st.markdown("##### General Metrics")
        col1, col2 = st.columns(2)
        duration_ms = audio_results['duration_seconds'] * 1000
        col1.metric("Analyzed Duration", f"{duration_ms:.1f} ms")
        col2.metric("Sample Count", f"{audio_results['sample_count']:,}")
        
        st.markdown("##### Quality Metrics")
        st.write(f"**DC Offset:** `{audio_results['dc_offset']:.8f}`")
        st.caption("A measure of direct current offset in the signal. Should be very close to zero for high-quality audio.")
        progress_val = min(1.0, abs(audio_results['dc_offset']) * 1000)  # Arbitrary scaling for visualization
        st.progress(progress_val)
    
    @staticmethod
    def render_waveform_plot(samples: np.ndarray, sample_rate: int, time_range: float):
        """Renders waveform plot."""
        st.subheader("Waveform Plot")
        
        max_samples = int(min(len(samples), time_range * sample_rate))
        waveform = samples[:max_samples]
        t = np.arange(max_samples) / sample_rate
        
        fig_wave = go.Figure(data=go.Scatter(x=t, y=waveform, mode='lines', name='Waveform'))
        fig_wave.update_layout(
            xaxis_title="Time (s)", 
            yaxis_title="Amplitude", 
            height=300, 
            margin=dict(l=20, r=20, t=30, b=20)
        )
        st.plotly_chart(fig_wave, use_container_width=True)
    
    @staticmethod
    def render_frequency_spectrum(samples: np.ndarray, sample_rate: int):
        """Renders frequency spectrum (FFT)."""
        st.subheader("Frequency Spectrum (FFT)")
        
        N = min(8192, len(samples))
        yf = np.abs(rfft(samples[:N]))
        xf = rfftfreq(N, 1 / sample_rate)
        
        fig_fft = go.Figure(data=go.Scatter(x=xf, y=20 * np.log10(yf + 1e-9), mode='lines', name='FFT'))
        fig_fft.update_layout(
            xaxis_title="Frequency (Hz)", 
            yaxis_title="Magnitude (dB)", 
            height=300, 
            margin=dict(l=20, r=20, t=30, b=20)
        )
        st.plotly_chart(fig_fft, use_container_width=True)


class DetailedPacketView:
    """View component for detailed packet log tab."""
    
    @staticmethod
    def render_legend():
        """Renders the legend and help information."""
        with st.expander("🔍 Display Legend & Help"):
            st.markdown("""
            **DBC Status Colors:**
            - 🟢 **Green**: DBC increment is correct (expected sequence)
            - 🔴 **Red**: DBC increment is incorrect (potential packet drop/duplicate)
            - 🔵 **Blue**: First packet in sequence (no previous reference)
            - 🟡 **Yellow**: No-data packet (DBC should increment by 8)
            - ⚪ **White**: Unknown/unanalyzed DBC status
            
            **Sample Data Colors:**
            - 🟠 **Orange**: Zero samples (silence/muted audio)
            - 🟢 **Green**: Non-zero audio data present
            - Gray code blocks: Normal hex values
            
            **CIP Header Fields:**
            - **FMT**: Format code (should be 0x00 for AM824)
            - **DBS**: Data Block Size (number of quadlets per data block)
            - **DBC**: Data Block Counter (increments by SYT_INTERVAL per data packet, +8 for no-data packets)
            - **FDF**: Format Dependent Field (contains sample rate info)
            - **SYT**: Synchronization Timestamp (0xFFFF for valid no-data packets)
            
            **No-Data Packet Validation:**
            - Valid no-data packets should have SYT=0xFFFF
            - SFC code in FDF should match the stream's sample rate (0x01 for 44.1kHz, 0x02 for 48kHz, etc.)
            - DBC should increment by 8 from the previous packet
            - Next data packet after no-data should NOT increment DBC (keeps same value as last data packet)
            """)
    
    @staticmethod
    def render_filter_controls(unique_channels: List[int]):
        """Renders filter controls and returns selected values."""
        col1, col2, col3 = st.columns(3)
        
        with col1:
            channel_filter = st.selectbox("Filter by Channel", 
                                        options=["All"] + unique_channels,
                                        index=0)
        with col2:
            packet_type_filter = st.selectbox("Filter by Type", 
                                            options=["All", "Data Packets Only", "Non-Data Packets Only"],
                                            index=0)
        with col3:
            max_packets = st.number_input("Max Packets to Display", min_value=1, max_value=1000, value=50)
        
        return channel_filter, packet_type_filter, max_packets
    
    @staticmethod
    def render_packet_pattern(pattern_analysis: dict):
        """Renders the data/no-data packet pattern with detailed analysis."""
        if not pattern_analysis or not pattern_analysis.get("pattern"):
            return
            
        st.subheader("📊 Packet Pattern Analysis")
        
        # Pattern overview
        col1, col2, col3, col4 = st.columns(4)
        col1.metric("Total Packets", pattern_analysis["total_packets"])
        col2.metric("Data Packets", pattern_analysis["data_packets"])
        col3.metric("No-Data Packets", pattern_analysis["no_data_packets"])
        col4.metric("Data %", f"{pattern_analysis['data_percentage']:.1f}%")
        
        # Pattern display
        st.info(f"**D** = Data packet, **N** = No-data packet")
        
        # Split pattern into chunks for better display
        pattern_parts = pattern_analysis["pattern"].split(' ')
        chunk_size = 25
        for i in range(0, len(pattern_parts), chunk_size):
            chunk = ' '.join(pattern_parts[i:i+chunk_size])
            packet_nums = ' '.join([f"{j+1:2d}" for j in range(i, min(i+chunk_size, len(pattern_parts)))])
            
            st.code(f"#{packet_nums}")
            st.code(f" {chunk}")
            if i + chunk_size < len(pattern_parts):
                st.write("")  # Add spacing between chunks
        
        # Sequence analysis
        if pattern_analysis.get("sequences"):
            with st.expander("🔍 Sequence Analysis"):
                st.write("**Consecutive packet sequences:**")
                for seq_type, length in pattern_analysis["sequences"]:
                    if len(seq_type) > 1:  # Only show sequences longer than 1
                        packet_type = "Data" if seq_type[0] == 'D' else "No-Data"
                        st.write(f"- {length} consecutive {packet_type} packets")
                    else:
                        packet_type = "Data" if seq_type[0] == 'D' else "No-Data"
                        st.write(f"- 1 {packet_type} packet")
    
    @staticmethod
    def render_statistics(stats: Dict[str, Any]):
        """Renders packet statistics."""
        col1, col2, col3, col4, col5 = st.columns(5)
        col1.metric("Total Filtered", stats["total_filtered"])
        col2.metric("Data Packets", stats["data_packets"])
        col3.metric("Zero Sample Packets", stats["zero_sample_packets"])
        col4.metric("🚨 Problem Packets", stats["problem_packets"], 
                   delta=f"{stats['problem_percent']:.1f}%" if stats["total_filtered"] > 0 else "0%")
        
        if stats["data_packets"] > 0:
            col5.metric("Zero Sample %", f"{stats['zero_sample_percent']:.1f}%")
        else:
            col5.metric("Zero Sample %", "N/A")
    
    @staticmethod
    def get_dbc_indicator(dbc_status: str) -> str:
        """Returns the appropriate DBC status indicator."""
        if dbc_status == "correct":
            return "🟢"
        elif dbc_status == "incorrect":
            return "🔴"
        elif dbc_status == "first":
            return "🔵"
        elif dbc_status == "no-data":
            return "🟡"
        else:
            return "⚪"
    
    @staticmethod
    def render_packet_details(packets: List[CIPPacket]):
        """Renders detailed packet information."""
        st.subheader(f"Packet Details")
        
        for i, packet in enumerate(packets):
            if not packet.is_valid:
                continue
                
            # Check for packet issues to determine styling
            has_issues = False
            issue_reasons = []
            
            # Check DBC status for all packets
            dbc_status = getattr(packet, 'dbc_status', None)
            if dbc_status == "incorrect":
                has_issues = True
                issue_reasons.append("Wrong DBC increment")
            
            if not packet.is_data_packet:
                # For no-data packets, check validation
                if hasattr(packet, 'no_data_issues') and packet.no_data_issues:
                    has_issues = True
                    issue_reasons.extend(packet.no_data_issues)
            else:
                # For data packets, check for zero samples
                if getattr(packet, 'samples_are_zero', False):
                    has_issues = True
                    issue_reasons.append("All audio samples are zero")
            
            # Create expandable section for each packet with styling
            timestamp_str = packet.get_timestamp_string() if hasattr(packet, 'get_timestamp_string') else 'Unknown'
            packet_type = 'Data' if packet.is_data_packet else 'No-Data'
            
            # Create title with DBC status indicator
            dbc_indicator = DetailedPacketView.get_dbc_indicator(dbc_status)
            
            if has_issues:
                expander_title = f"❌ {dbc_indicator} Packet {i+1}: {packet_type} | Ch {packet.channel} | {timestamp_str} | Issues: {', '.join(issue_reasons)}"
            else:
                expander_title = f"✅ {dbc_indicator} Packet {i+1}: {packet_type} | Ch {packet.channel} | {timestamp_str}"
            
            with st.expander(expander_title, expanded=False):
                DetailedPacketView._render_single_packet_details(packet, packet_type)
    
    @staticmethod
    def _render_single_packet_details(packet: CIPPacket, packet_type: str):
        """Renders details for a single packet."""
        # CIP Header breakdown
        st.subheader("CIP Header")
        header = packet.get_cip_header_breakdown()
        if header:
            col1, col2, col3, col4, col5 = st.columns(5)
            col1.metric("FMT", header['fmt'])
            col2.metric("DBS", header['dbs'])
            col3.metric("DBC", header['dbc'])
            col4.metric("FDF", header['fdf'])
            col5.metric("SYT", header['syt'])
            
            # Show raw header words
            st.code(f"Word 1: {header['word1']}    Word 2: {header['word2']}")
        
        # Packet-specific analysis
        if packet_type == "Data":
            DetailedPacketView._render_data_packet_analysis(packet)
        else:
            DetailedPacketView._render_no_data_packet_analysis(packet)
        
        # Raw hex dump
        st.subheader("Raw Hex Data")
        hex_dump = packet.hex_data
        if hex_dump:
            words = hex_dump.split(' ')
            for i in range(0, len(words), 4):
                line_words = words[i:i+4]
                hex_line = ' '.join(line_words)
                st.code(f"{i//4*16:04X}   {hex_line}")
        else:
            st.write("No hex data available")
        
        st.divider()
    
    @staticmethod
    def _render_data_packet_analysis(packet: CIPPacket):
        """Renders analysis for data packets."""
        st.subheader("Audio Data Analysis")
        
        if len(packet.audio_samples) > 0:
            col1, col2, col3 = st.columns(3)
            col1.metric("Sample Count", len(packet.audio_samples))
            col2.metric("Peak Level", f"{np.max(np.abs(packet.audio_samples)):.4f}")
            col3.metric("All Zero?", "Yes" if packet.samples_are_zero else "No")
            
            # Show first few samples
            if len(packet.audio_samples_hex) > 0:
                st.subheader("Audio Sample Data (First 8 samples)")
                sample_text = ""
                for j, (hex_val, float_val) in enumerate(zip(packet.audio_samples_hex[:8], packet.audio_samples[:8])):
                    if packet.samples_are_zero:
                        sample_text += f"🟠 {hex_val} ({float_val:.6f})  "
                    else:
                        sample_text += f"🟢 {hex_val} ({float_val:.6f})  "
                    if (j + 1) % 4 == 0:
                        sample_text += "\n"
                st.code(sample_text)
        else:
            st.warning("No audio samples found in this packet")
    
    @staticmethod
    def _render_no_data_packet_analysis(packet: CIPPacket):
        """Renders analysis for no-data packets."""
        st.subheader("No-Data Packet Validation")
        
        # Check if it's a valid no-data packet
        if packet.syt == 0xFFFF:
            st.success("✅ Valid no-data packet: SYT = 0xFFFF")
        else:
            st.warning(f"⚠️ Invalid no-data packet: SYT = 0x{packet.syt:04X} (should be 0xFFFF)")
        
        # Check SFC code in FDF
        sfc_code = packet.fdf & 0b00000111
        sfc_map = {0: "32kHz", 1: "44.1kHz", 2: "48kHz", 3: "88.2kHz", 4: "96kHz", 5: "176.4kHz", 6: "192kHz"}
        sfc_description = sfc_map.get(sfc_code, f"Unknown ({sfc_code})")
        
        col1, col2 = st.columns(2)
        col1.metric("SFC Code", f"{sfc_code} ({sfc_description})")
        col2.metric("FDF Type", f"0x{(packet.fdf >> 4) & 0x0F:X}")
        
        if packet.fdf == 0xFF:
            st.warning("⚠️ FDF = 0xFF indicates legacy/invalid no-data packet format")
        elif sfc_code in sfc_map:
            st.success(f"✅ Valid SFC code for {sfc_description}")
        else:
            st.error(f"❌ Invalid SFC code: {sfc_code}")
        
        # Show validation issues if any
        if hasattr(packet, 'no_data_issues') and packet.no_data_issues:
            st.error("❌ No-data packet validation issues:")
            for issue in packet.no_data_issues:
                st.write(f"- {issue}")
        
        st.info("ℹ️ No-data packets contain no audio samples and are used for timing/synchronization.")
