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
tab_cip, tab_audio = st.tabs(["📦 CIP Packet Analysis", "🔊 Audio Waveform Analysis"])

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