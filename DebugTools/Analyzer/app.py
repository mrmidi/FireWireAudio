# app_mvc.py - Refactored using MVC pattern
import streamlit as st
import numpy as np
from mvc.controller import AppController
from mvc.views import CIPAnalysisView, AudioAnalysisView, DetailedPacketView, WaveAnalysisView
from mvc.packet_analysis_views import PacketAnalysisView
from mvc.anomality_views import AudioAnomalyView
from firewire.audio_anomality_analyzer import AudioAnomalyAnalyzer

# --- Page Configuration ---
st.set_page_config(page_title="FireWire Audio Packet Analyzer", layout="wide")
st.title("🎵 FireWire Audio & Packet Analyzer")
st.caption("Optimized for Apple FireBug 2.3 log format")

# Initialize controller
if 'controller' not in st.session_state:
    st.session_state.controller = AppController()

controller = st.session_state.controller

# --- File Uploader ---
uploaded_files = st.file_uploader(
    "Upload FireBug .txt or .log files", type=["txt", "log"], accept_multiple_files=True
)

if not uploaded_files:
    st.info("Please upload one or more log files to begin analysis.")
    st.stop()

# --- Parsing and Analysis ---
with st.spinner("Parsing log files and analyzing packets..."):
    if not controller.load_files(uploaded_files):
        st.stop()

# --- UI TABS ---
tab_packet, tab_audio, tab_wave, tab_detail = st.tabs(["📦 Packet Analyzer", "🔊 Audio Waveform Analysis", "🌊 Wave File Analysis", "🔍 Detailed Packet Log"])

# --- TAB 1: Enhanced Packet Analysis ---
with tab_packet:
    # Channel selection for analysis
    unique_channels = controller.get_unique_channels()
    if unique_channels:
        channel_select = st.selectbox(
            "Select Channel for Analysis", 
            options=[None] + unique_channels, 
            format_func=lambda x: "All Channels" if x is None else f"Channel {x}",
            index=0
        )
    else:
        channel_select = None
    
    # Get comprehensive packet analysis
    with st.spinner("Analyzing packet anomalies..."):
        analysis_results = controller.get_comprehensive_packet_analysis(channel_select)
        packet_samples = controller.export_packet_samples(max_samples_per_type=5)
    
    # Render analysis results
    PacketAnalysisView.render_packet_health_overview(analysis_results)
    
    # Detailed analyses in expandable sections
    PacketAnalysisView.render_dbc_analysis_enhanced(analysis_results.get("dbc_analysis", {}))
    PacketAnalysisView.render_length_errors(analysis_results.get("length_errors", {}))
    PacketAnalysisView.render_dropout_analysis(analysis_results.get("dropout_analysis", {}))
    PacketAnalysisView.render_pattern_analysis(analysis_results.get("pattern_analysis", {}))
    
    # Timeline and channel comparison
    PacketAnalysisView.render_packet_timeline(analysis_results)
    PacketAnalysisView.render_channel_comparison(analysis_results)
    
    # Export controls
    PacketAnalysisView.render_export_controls(analysis_results, packet_samples)

# --- TAB 2: Audio Analysis ---
with tab_audio:
    st.header("Audio Waveform Analysis")
    
    unique_channels = controller.get_unique_channels()
    if not unique_channels:
        st.warning("No data packets with audio channels found.")
        st.stop()
    
    # Audio controls
    channel_select, time_range = AudioAnalysisView.render_audio_controls(unique_channels)
    
    # Get and analyze audio samples
    samples = controller.get_audio_samples(channel_select=channel_select)
    if len(samples) == 0:
        st.warning(f"No audio samples found for channel {channel_select}.")
    else:
        # --- Anomaly Analysis Integration ---
        anomaly_analyzer = AudioAnomalyAnalyzer(controller.analyzer)
        anomaly_report = anomaly_analyzer.comprehensive_audio_quality_report(channel_select=channel_select)
        AudioAnomalyView.render_comprehensive_report(anomaly_report)
        # --- End Anomaly Analysis ---
        
        # Render audio metrics
        audio_results = controller.get_audio_analysis(samples)
        AudioAnalysisView.render_audio_metrics(audio_results)
        
        # Render waveform plot
        AudioAnalysisView.render_waveform_plot(samples, controller.get_sample_rate(), time_range)
        
        # Render frequency spectrum
        AudioAnalysisView.render_frequency_spectrum(samples, controller.get_sample_rate())

# --- TAB 3: Wave File Analysis ---
with tab_wave:
    st.header("Wave File Analysis")
    st.caption("Upload and analyze WAV files for transients, dropouts, and spectral content")
    
    # File uploader
    wave_file = WaveAnalysisView.render_file_uploader()
    
    if wave_file:
        # Load the file
        with st.spinner("Loading WAV file..."):
            if not controller.load_wave_file(wave_file):
                st.stop()
        
        # Configuration controls
        config = WaveAnalysisView.render_config_controls()
        controller.update_wave_config(config)
        
        # Get and display audio metrics
        with st.spinner("Analyzing audio metrics..."):
            metrics = controller.get_wave_metrics()
            if "error" not in metrics:
                WaveAnalysisView.render_audio_info(metrics)
                WaveAnalysisView.render_channel_metrics(metrics)
        
        # Event analysis
        with st.spinner("Detecting events (transients and dropouts)..."):
            events_result = controller.analyze_wave_events()
            WaveAnalysisView.render_event_analysis(events_result)
        
        # Clustering analysis
        if events_result.get('transients', 0) > 0:
            with st.spinner("Clustering transients..."):
                clusters_result = controller.cluster_wave_transients()
                WaveAnalysisView.render_clustering_results(clusters_result)
        
        # Spectrogram
        if metrics.get('channels', 0) > 0:
            st.divider()
            channel_idx, channel_label = WaveAnalysisView.render_channel_selector(metrics['channels'])
            
            with st.spinner(f"Generating spectrogram for {channel_label}..."):
                try:
                    frequencies, times, spectrogram = controller.get_wave_spectrogram(channel_idx)
                    if frequencies is not None:
                        WaveAnalysisView.render_spectrogram(frequencies, times, spectrogram, channel_label)
                except Exception as e:
                    st.error(f"Error generating spectrogram: {str(e)}")
    else:
        st.info("Please upload a WAV file to begin analysis.")

# --- TAB 4: Detailed Packet Log ---
with tab_detail:
    st.header("Detailed CIP Packet Log")
    st.caption("Inspect individual packets with CIP header breakdown and sample data highlighting")
    
    # Legend/Help
    DetailedPacketView.render_legend()
    
    # Run DBC analysis to set packet status (if not already done)
    controller.get_dbc_analysis()
    controller.get_packet_type_analysis()
    
    # Filter controls
    unique_channels = [p.channel for p in controller.analyzer.all_packets if p.is_valid]
    unique_channels = sorted(list(set(unique_channels)))
    channel_filter, packet_type_filter, max_packets = DetailedPacketView.render_filter_controls(unique_channels)
    
    # Get filtered packets
    filtered_packets = controller.filter_packets(channel_filter, packet_type_filter, max_packets)
    
    # Display packet pattern
    pattern_analysis = controller.get_packet_pattern_analysis(num_packets=50)
    DetailedPacketView.render_packet_pattern(pattern_analysis)
    
    # Display statistics
    stats = controller.calculate_packet_statistics(filtered_packets)
    DetailedPacketView.render_statistics(stats)
    
    # Display packet details
    DetailedPacketView.render_packet_details(filtered_packets)
