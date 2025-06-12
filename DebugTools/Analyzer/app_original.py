# app.py
import streamlit as st
import numpy as np
import plotly.graph_objects as go
from scipy.fft import rfft, rfftfreq

from firewire.log_parser import parse_log_content
from firewire.analyzer import Analyzer

# --- Page Configuration ---
st.set_page_config(page_title="FireWire Audio Packet Analyzer", layout="wide")
st.title("🎵 FireWire Audio & Packet Analyzer")
st.caption("Optimized for Apple FireBug 2.3 log format")

# --- File Uploader ---
uploaded_files = st.file_uploader(
    "Upload FireBug .txt or .log files", type=["txt", "log"], accept_multiple_files=True
)

if not uploaded_files:
    st.info("Please upload one or more log files to begin analysis.")
    st.stop()

# --- Parsing and Analysis ---
with st.spinner("Parsing log files and analyzing packets..."):
    all_content = "".join([file.read().decode('utf-8', errors='ignore') for file in uploaded_files])
    
    packets = parse_log_content(all_content)
    if not packets:
        st.error("No valid FireWire packets could be parsed from the uploaded files.")
        st.stop()
    
    st.success(f"Successfully parsed {len(packets)} total packets from {len(uploaded_files)} file(s).")
    analyzer = Analyzer(packets)

# --- UI TABS ---
tab_cip, tab_audio, tab_detail = st.tabs(["📦 CIP Packet Analysis", "🔊 Audio Waveform Analysis", "🔍 Detailed Packet Log"])

# --- TAB 1: CIP Packet Analysis ---
with tab_cip:

    st.header("Stream Format (from first data packet)")
    fmt_results = analyzer.format_info
    if "error" in fmt_results:
        st.warning(fmt_results["error"])
    else:
        col1, col2, col3, col4 = st.columns(4)
        col1.metric("Sample Rate", f"{fmt_results['nominal_sample_rate']/1000:.1f} kHz")
        col2.metric("Subformat Type", fmt_results['subformat_type'])
        col3.metric("SYT Interval", str(fmt_results['syt_interval']), help="Number of audio frames per isochronous packet.")
        col4.metric("FDF/SFC Code", f"{fmt_results['fdf_full_hex']} / {fmt_results['sfc_code']}", help="Format-Dependent Field / Sampling Frequency Code")

    st.subheader("Packet Type Distribution")
    packet_type_results = analyzer.analyze_packet_types()
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

    st.subheader("Data Block Counter (DBC) Continuity")
    dbc_results = analyzer.analyze_dbc_continuity()
    if not dbc_results["discontinuities"]:
        st.success(
            f"✅ DBC is continuous for all detected channels: {dbc_results['channels_found']}. "
            f"Expected increment is {dbc_results['expected_increment']}."
        )
    else:
        st.error(f"🚨 Found {len(dbc_results['discontinuities'])} DBC discontinuities (potential packet drops).")
        with st.expander("Show Discontinuity Details"):
            st.dataframe(dbc_results["discontinuities"])

    st.subheader("Synchronization Timestamp (SYT) Analysis")
    syt_results = analyzer.analyze_syt_timestamp()
    if "error" in syt_results:
        st.warning(syt_results["error"])
    else:
        col1, col2, col3, col4 = st.columns(4)
        col1.metric("Mean SYT Delta", f"{syt_results['mean_syt_delta']:.2f}")
        col2.metric("Std Dev (Jitter)", f"{syt_results['std_dev_syt_delta (jitter)']:.2f}")
        col3.metric("Min Delta", f"{syt_results['min_syt_delta']}")
        col4.metric("Max Delta", f"{syt_results['max_syt_delta']}")
        st.caption(f"Theoretical SYT delta per packet: {syt_results['theoretical_syt_delta_per_packet']}")


# --- TAB 2: Audio Analysis ---
with tab_audio:
    st.header("Audio Waveform Analysis")
    
    unique_channels = sorted(list(set(p.channel for p in analyzer.data_packets)))
    if not unique_channels:
        st.warning("No data packets with audio channels found.")
        st.stop()
    col1, col2 = st.columns(2)
    channel_select = col1.selectbox("Select Audio Channel", options=unique_channels, index=0)
    time_range = col2.slider("Time Range to Display (seconds)", 0.1, 5.0, 1.0, 0.1)
    samples = analyzer.get_aggregated_audio_samples(channel_select=channel_select)
    if len(samples) == 0:
        st.warning(f"No audio samples found for channel {channel_select}.")
    else:
        audio_results = analyzer.analyze_audio_waveform(samples)
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
        progress_val = min(1.0, abs(audio_results['dc_offset']) * 1000) # Arbitrary scaling for visualization
        st.progress(progress_val)
        st.subheader("Waveform Plot")
        max_samples = int(min(len(samples), time_range * analyzer.sample_rate))
        waveform = samples[:max_samples]
        t = np.arange(max_samples) / analyzer.sample_rate
        fig_wave = go.Figure(data=go.Scatter(x=t, y=waveform, mode='lines', name='Waveform'))
        fig_wave.update_layout(xaxis_title="Time (s)", yaxis_title="Amplitude", height=300, margin=dict(l=20, r=20, t=30, b=20))
        st.plotly_chart(fig_wave, use_container_width=True)
        st.subheader("Frequency Spectrum (FFT)")
        N = min(8192, len(waveform))
        yf = np.abs(rfft(waveform[:N]))
        xf = rfftfreq(N, 1 / analyzer.sample_rate)
        fig_fft = go.Figure(data=go.Scatter(x=xf, y=20 * np.log10(yf + 1e-9), mode='lines', name='FFT'))
        fig_fft.update_layout(xaxis_title="Frequency (Hz)", yaxis_title="Magnitude (dB)", height=300, margin=dict(l=20, r=20, t=30, b=20))
        st.plotly_chart(fig_fft, use_container_width=True)

# --- TAB 3: Detailed Packet Log ---
with tab_detail:
    st.header("Detailed CIP Packet Log")
    st.caption("Inspect individual packets with CIP header breakdown and sample data highlighting")
    
    # Legend/Help
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
    
    # Run DBC analysis to set packet status
    dbc_results = analyzer.analyze_dbc_continuity()
    packet_type_results = analyzer.analyze_packet_types()
    
    # Filter controls
    col1, col2, col3 = st.columns(3)
    with col1:
        channel_filter = st.selectbox("Filter by Channel", 
                                    options=["All"] + sorted(list(set(p.channel for p in analyzer.all_packets if p.is_valid))),
                                    index=0)
    with col2:
        packet_type_filter = st.selectbox("Filter by Type", 
                                        options=["All", "Data Packets Only", "Non-Data Packets Only"],
                                        index=0)
    with col3:
        max_packets = st.number_input("Max Packets to Display", min_value=1, max_value=1000, value=50)
    
    # Filter packets
    filtered_packets = analyzer.all_packets
    if channel_filter != "All":
        filtered_packets = [p for p in filtered_packets if p.channel == channel_filter]
    if packet_type_filter == "Data Packets Only":
        filtered_packets = [p for p in filtered_packets if p.is_data_packet]
    elif packet_type_filter == "Non-Data Packets Only":
        filtered_packets = [p for p in filtered_packets if not p.is_data_packet]
    
    # Limit to max packets
    filtered_packets = filtered_packets[:max_packets]
    
    # Summary statistics with problem detection
    total_data_packets = sum(1 for p in filtered_packets if p.is_data_packet)
    total_zero_sample_packets = sum(1 for p in filtered_packets if getattr(p, 'samples_are_zero', False))
    
    # Count problematic packets with updated logic
    problematic_packets = 0
    for p in filtered_packets:
        dbc_status = getattr(p, 'dbc_status', None)
        if dbc_status == "incorrect":
            problematic_packets += 1
        elif not p.is_data_packet and hasattr(p, 'no_data_issues'):
            problematic_packets += 1
        elif p.is_data_packet and getattr(p, 'samples_are_zero', False):
            problematic_packets += 1
    
    col1, col2, col3, col4, col5 = st.columns(5)
    col1.metric("Total Filtered", len(filtered_packets))
    col2.metric("Data Packets", total_data_packets)
    col3.metric("Zero Sample Packets", total_zero_sample_packets)
    col4.metric("🚨 Problem Packets", problematic_packets, delta=f"{(problematic_packets/len(filtered_packets)*100):.1f}%" if len(filtered_packets) > 0 else "0%")
    if total_data_packets > 0:
        col5.metric("Zero Sample %", f"{(total_zero_sample_packets/total_data_packets)*100:.1f}%")
    else:
        col5.metric("Zero Sample %", "N/A")
    
    st.subheader(f"Packet Details")
    
    # Display packets
    for i, packet in enumerate(filtered_packets):
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
                issue_reasons.append("All samples zero")
        
        # Create expandable section for each packet with styling
        timestamp_str = packet.get_timestamp_string() if hasattr(packet, 'get_timestamp_string') else 'Unknown'
        packet_type = 'Data' if packet.is_data_packet else 'No-Data'
        
        # Create title with DBC status indicator
        if dbc_status == "correct":
            dbc_indicator = "🟢"
        elif dbc_status == "incorrect":
            dbc_indicator = "🔴"
        elif dbc_status == "first":
            dbc_indicator = "🔵"
        elif dbc_status == "no-data":
            dbc_indicator = "🟡"
        else:
            dbc_indicator = "⚪"
        
        if has_issues:
            expander_title = f"🚨 {dbc_indicator} Packet {getattr(packet, 'original_index', i)} - {timestamp_str} - Channel {packet.channel} - {packet_type} - ISSUES: {', '.join(issue_reasons)}"
        else:
            expander_title = f"✅ {dbc_indicator} Packet {getattr(packet, 'original_index', i)} - {timestamp_str} - Channel {packet.channel} - {packet_type}"
        if has_issues:
            expander_title = f"🚨 {dbc_indicator} Packet {getattr(packet, 'original_index', i)} - {timestamp_str} - Channel {packet.channel} - {packet_type} - ISSUES: {', '.join(issue_reasons)}"
            # Add red background styling for problematic packets
            st.markdown(
                f"""
                <div style="border: 2px solid #ff4444; border-radius: 5px; padding: 10px; margin: 5px 0; background-color: rgba(255, 68, 68, 0.1);">
                    <strong>🚨 PACKET ISSUES DETECTED:</strong> {', '.join(issue_reasons)}
                </div>
                """, 
                unsafe_allow_html=True
            )
        else:
            expander_title = f"✅ {dbc_indicator} Packet {getattr(packet, 'original_index', i)} - {timestamp_str} - Channel {packet.channel} - {packet_type}"
        
        with st.expander(expander_title, expanded=False):
            
            # Basic packet info
            col1, col2, col3, col4, col5 = st.columns(5)
            col1.metric("Channel", packet.channel)
            col2.metric("Tag", packet.tag) 
            col3.metric("SY", packet.sy)
            col4.metric("Size", f"{packet.header_size} [{packet.actual_size}]")
            col5.metric("Timestamp", packet.get_timestamp_string() if hasattr(packet, 'get_timestamp_string') else 'Unknown')
            
            # CIP Header breakdown (for both data and no-data packets)
            st.subheader("CIP Header Breakdown")
            header_data = packet.get_cip_header_breakdown()
            if header_data:
                col1, col2, col3, col4, col5 = st.columns(5)
                
                # FMT field
                col1.metric("FMT", header_data['fmt'])
                
                # DBS field  
                col2.metric("DBS", header_data['dbs'])
                
                # DBC field with color coding
                dbc_status = getattr(packet, 'dbc_status', 'unknown')
                if dbc_status == "correct":
                    col3.markdown(f"**DBC** 🟢  \n{header_data['dbc']}")
                elif dbc_status == "incorrect":
                    col3.markdown(f"**DBC** 🔴  \n{header_data['dbc']}")
                elif dbc_status == "first":
                    col3.markdown(f"**DBC** 🔵  \n{header_data['dbc']}")
                elif dbc_status == "no-data":
                    col3.markdown(f"**DBC** 🟡  \n{header_data['dbc']}")
                else:
                    col3.metric("DBC", header_data['dbc'])
                
                # FDF field
                col4.metric("FDF", header_data['fdf'])
                
                # SYT field
                col5.metric("SYT", header_data['syt'])
            
            # Raw CIP header words
            st.subheader("Raw CIP Header")
            if len(packet.hex_words) >= 2:
                col1, col2 = st.columns(2)
                col1.code(f"Word 1: {packet.hex_words[0]}")
                col2.code(f"Word 2: {packet.hex_words[1]}")
            
            if packet.is_data_packet:
                st.subheader("Audio Samples")
                if packet.samples_are_zero:
                    st.markdown("🟠 **All samples are zero (silence detected)**")
                else:
                    st.markdown("🟢 **Audio data present**")
                
                # Display sample data in a grid
                if packet.audio_samples_hex:
                    st.write(f"**{len(packet.audio_samples_hex)} samples in hex:**")
                    
                    # Group samples in rows of 8 for better display
                    samples_per_row = 8
                    for row_start in range(0, len(packet.audio_samples_hex), samples_per_row):
                        row_samples = packet.audio_samples_hex[row_start:row_start + samples_per_row]
                        cols = st.columns(len(row_samples))
                        for j, sample_hex in enumerate(row_samples):
                            if sample_hex == "00000000":
                                cols[j].markdown(f"🟠 `{sample_hex}`")
                            else:
                                cols[j].code(sample_hex)
                
                # Audio sample values (normalized)
                if len(packet.audio_samples) > 0:
                    st.subheader("Normalized Audio Values")
                    sample_stats = {
                        "Count": len(packet.audio_samples),
                        "Min": f"{np.min(packet.audio_samples):.6f}",
                        "Max": f"{np.max(packet.audio_samples):.6f}",
                        "Mean": f"{np.mean(packet.audio_samples):.6f}",
                        "RMS": f"{np.sqrt(np.mean(packet.audio_samples**2)):.6f}"
                    }
                    col1, col2, col3, col4, col5 = st.columns(5)
                    col1.metric("Count", sample_stats["Count"])
                    col2.metric("Min", sample_stats["Min"])
                    col3.metric("Max", sample_stats["Max"])
                    col4.metric("Mean", sample_stats["Mean"])
                    col5.metric("RMS", sample_stats["RMS"])
            
            else:
                # No-data packet information
                st.subheader("No-Data Packet Analysis")
                
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
                
            # Raw hex dump
            st.subheader("Raw Hex Data")
            hex_dump = packet.hex_data
            if hex_dump:
                # Format hex dump similar to FireBug output
                words = hex_dump.split(' ')
                for i in range(0, len(words), 4):
                    line_words = words[i:i+4]
                    hex_line = ' '.join(line_words)
                    st.code(f"{i//4*16:04X}   {hex_line}")
            else:
                st.write("No hex data available")
            
            st.divider()