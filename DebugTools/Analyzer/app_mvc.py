# app_mvc.py - Refactored using MVC pattern
import streamlit as st
import numpy as np
from mvc.controller import AppController
from mvc.views import CIPAnalysisView, AudioAnalysisView, DetailedPacketView

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
tab_cip, tab_audio, tab_detail = st.tabs(["📦 CIP Packet Analysis", "🔊 Audio Waveform Analysis", "🔍 Detailed Packet Log"])

# --- TAB 1: CIP Packet Analysis ---
with tab_cip:
    # Stream Format
    fmt_results = controller.get_format_info()
    CIPAnalysisView.render_format_info(fmt_results)
    
    # Packet Type Distribution
    packet_type_results = controller.get_packet_type_analysis()
    CIPAnalysisView.render_packet_types(packet_type_results)
    
    # DBC Continuity
    dbc_results = controller.get_dbc_analysis()
    CIPAnalysisView.render_dbc_analysis(dbc_results)
    
    # SYT Analysis
    syt_results = controller.get_syt_analysis()
    CIPAnalysisView.render_syt_analysis(syt_results)

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
        audio_results = controller.get_audio_analysis(samples)
        
        # Render audio metrics
        AudioAnalysisView.render_audio_metrics(audio_results)
        
        # Render waveform plot
        AudioAnalysisView.render_waveform_plot(samples, controller.get_sample_rate(), time_range)
        
        # Render frequency spectrum
        AudioAnalysisView.render_frequency_spectrum(samples, controller.get_sample_rate())

# --- TAB 3: Detailed Packet Log ---
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
    pattern = controller.get_packet_pattern(num_packets=50)
    DetailedPacketView.render_packet_pattern(pattern)
    
    # Display statistics
    stats = controller.calculate_packet_statistics(filtered_packets)
    DetailedPacketView.render_statistics(stats)
    
    # Display packet details
    DetailedPacketView.render_packet_details(filtered_packets)
