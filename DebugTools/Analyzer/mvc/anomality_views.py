# mvc/anomaly_views.py
import streamlit as st
import numpy as np
import plotly.graph_objects as go
from plotly.subplots import make_subplots
from typing import Dict, Any, List

class AudioAnomalyView:
    """View component for audio anomaly analysis."""
    
    @staticmethod
    def render_comprehensive_report(report: Dict[str, Any]):
        """Renders the comprehensive audio quality report."""
        st.header("🔍 Comprehensive Audio Quality Analysis")
        
        if "error" in report:
            st.error(report["error"])
            return
        
        # Overall quality score
        col1, col2, col3 = st.columns(3)
        
        quality_score = report["overall_quality_score"]
        quality_grade = report["quality_grade"]
        
        # Color-code the quality score
        if quality_score >= 90:
            col1.success(f"**Quality Score:** {quality_score}/100")
        elif quality_score >= 70:
            col1.warning(f"**Quality Score:** {quality_score}/100")
        else:
            col1.error(f"**Quality Score:** {quality_score}/100")
        
        col2.metric("Quality Grade", quality_grade)
        
        # Issues summary
        if report["issues_summary"]:
            col3.error("**Issues Found:**")
            for issue in report["issues_summary"]:
                col3.write(f"• {issue}")
        else:
            col3.success("**No major issues detected**")
        
        # Recommendations
        st.subheader("💡 Recommendations")
        for i, rec in enumerate(report["recommendations"], 1):
            st.info(f"**{i}.** {rec}")
        
        # Detailed analysis sections
        detailed = report.get("detailed_analysis", {})
        
        # DBC-Audio Correlation Analysis
        if "correlation_analysis" in detailed:
            AudioAnomalyView.render_dbc_correlation_analysis(detailed["correlation_analysis"])
        
        # Click/Pop Analysis
        if "click_analysis" in detailed:
            AudioAnomalyView.render_click_analysis(detailed["click_analysis"])
    
    @staticmethod
    def render_dbc_correlation_analysis(correlation_data: Dict[str, Any]):
        """Renders DBC-Audio correlation analysis."""
        st.subheader("📊 DBC-Audio Quality Correlation (Critical for OXFW971)")
        
        correlation_stats = correlation_data.get("correlation_stats", {})
        
        # Statistics overview
        col1, col2, col3, col4 = st.columns(4)
        col1.metric("DBC Issues", correlation_stats.get("total_dbc_issues", 0))
        col2.metric("Audio Impact", correlation_stats.get("dbc_issues_with_audio_impact", 0))
        col3.metric("Correlation %", f"{correlation_stats.get('correlation_percentage', 0):.1f}%")
        col4.metric("Spectral Anomalies", correlation_stats.get("total_spectral_anomalies", 0))
        
        # Spectral analysis
        spectral_analysis = correlation_data.get("spectral_analysis", {})
        if spectral_analysis and not "error" in spectral_analysis:
            AudioAnomalyView.render_spectral_anomalies(spectral_analysis)
        
        # Inter-packet continuity
        continuity_analysis = correlation_data.get("continuity_analysis", {})
        if continuity_analysis and not "error" in continuity_analysis:
            AudioAnomalyView.render_continuity_analysis(continuity_analysis)
        
        # Correlation details
        correlations = correlation_data.get("correlations", [])
        if correlations:
            with st.expander("🔗 Detailed DBC-Audio Correlations"):
                for i, corr in enumerate(correlations):
                    dbc_issue = corr["dbc_issue"]
                    has_impact = corr["has_audio_impact"]
                    
                    if has_impact:
                        st.error(f"**Correlation {i+1}:** {corr['description']}")
                    else:
                        st.info(f"**Correlation {i+1}:** {corr['description']} (no audio impact)")
                    
                    # Show DBC issue details
                    st.write(f"• **DBC Issue:** {dbc_issue.get('description', 'Unknown')}")
                    st.write(f"• **Packet:** {dbc_issue.get('packet_index', 'Unknown')}")
                    st.write(f"• **Channel:** {dbc_issue.get('channel', 'Unknown')}")
                    
                    # Show related audio issues
                    audio_issues = corr.get("related_audio_issues", [])
                    if audio_issues:
                        st.write(f"• **Related Audio Issues:** {len(audio_issues)}")
                        for issue in audio_issues:
                            st.write(f"  - {issue.get('type', 'Unknown')} at {issue.get('time_ms', 0):.1f}ms")
                    
                    st.divider()
    
    @staticmethod
    def render_spectral_anomalies(spectral_data: Dict[str, Any]):
        """Renders spectral anomaly analysis."""
        st.subheader("🌊 Spectral Anomaly Analysis")
        
        anomalies = spectral_data.get("anomalies", [])
        
        if not anomalies:
            st.success("✅ No spectral anomalies detected")
            return
        
        # Group anomalies by severity
        high_severity = [a for a in anomalies if a.get("severity") == "high"]
        moderate_severity = [a for a in anomalies if a.get("severity") == "moderate"]
        
        col1, col2, col3 = st.columns(3)
        col1.metric("Total Anomalies", len(anomalies))
        col2.error(f"High Severity: {len(high_severity)}")
        col3.warning(f"Moderate Severity: {len(moderate_severity)}")
        
        # Display anomaly details
        for anomaly in anomalies:
            severity = anomaly.get("severity", "unknown")
            if severity == "high":
                st.error(f"🚨 **{anomaly.get('type', 'Unknown')}:** {anomaly.get('description', 'No description')}")
            else:
                st.warning(f"⚠️ **{anomaly.get('type', 'Unknown')}:** {anomaly.get('description', 'No description')}")
        
        # Plot spectrum with anomalies highlighted
        if "spectrum_db" in spectral_data and "frequencies" in spectral_data:
            AudioAnomalyView.render_spectrum_plot(spectral_data, anomalies)
    
    @staticmethod
    def render_spectrum_plot(spectral_data: Dict[str, Any], anomalies: List[Dict]):
        """Renders spectrum plot with anomalies highlighted."""
        spectrum_db = spectral_data["spectrum_db"]
        frequencies = spectral_data["frequencies"]
        
        fig = go.Figure()
        
        # Main spectrum
        fig.add_trace(go.Scatter(
            x=frequencies,
            y=spectrum_db,
            mode='lines',
            name='Spectrum',
            line=dict(color='blue')
        ))
        
        # Highlight anomaly regions
        nyquist = frequencies[-1]
        high_freq_start = nyquist * 0.8
        ultrasonic_start = nyquist * 0.9
        
        # Add shaded regions for anomaly detection bands
        fig.add_vrect(
            x0=high_freq_start, x1=ultrasonic_start,
            fillcolor="orange", opacity=0.2,
            annotation_text="High Freq Band", annotation_position="top left"
        )
        
        fig.add_vrect(
            x0=ultrasonic_start, x1=nyquist,
            fillcolor="red", opacity=0.2,
            annotation_text="Ultrasonic Band", annotation_position="top left"
        )
        
        # Mark specific frequency peaks
        for anomaly in anomalies:
            if anomaly.get("type") == "high_frequency_peak" and "frequency" in anomaly:
                freq = anomaly["frequency"]
                level = anomaly["level_db"]
                fig.add_trace(go.Scatter(
                    x=[freq], y=[level],
                    mode='markers',
                    marker=dict(size=10, color='red', symbol='x'),
                    name=f'Peak @ {freq:.0f}Hz',
                    showlegend=False
                ))
        
        fig.update_layout(
            title="Frequency Spectrum with Anomaly Detection Bands",
            xaxis_title="Frequency (Hz)",
            yaxis_title="Magnitude (dB)",
            height=400
        )
        
        st.plotly_chart(fig, use_container_width=True)
    
    @staticmethod
    def render_continuity_analysis(continuity_data: Dict[str, Any]):
        """Renders inter-packet continuity analysis."""
        st.subheader("🔗 Inter-Packet Audio Continuity")
        
        discontinuities = continuity_data.get("discontinuities", [])
        boundaries = continuity_data.get("packet_boundaries", [])
        
        col1, col2, col3 = st.columns(3)
        col1.metric("Packet Boundaries", continuity_data.get("total_boundaries", 0))
        col2.metric("Discontinuities", len(discontinuities))
        col3.metric("Max Jump", f"{continuity_data.get('max_jump', 0):.4f}")
        
        if discontinuities:
            st.error(f"🚨 {len(discontinuities)} audio discontinuities detected!")
            
            with st.expander("Show Discontinuity Details"):
                for disc in discontinuities:
                    st.write(f"**{disc.get('type', 'Unknown')}** at {disc.get('time_ms', 0):.1f}ms:")
                    st.write(f"• Sample position: {disc.get('sample_position', 0)}")
                    st.write(f"• Jump magnitude: {disc.get('jump_magnitude', 0):.4f}")
                    st.write(f"• Related to DBC issue: {'Yes' if disc.get('dbc_issue') else 'No'}")
                    st.divider()
        else:
            st.success("✅ No significant audio discontinuities detected")
        
        # Plot packet boundary jumps
        if boundaries:
            AudioAnomalyView.render_boundary_plot(boundaries)
    
    @staticmethod
    def render_boundary_plot(boundaries: List[Dict]):
        """Renders packet boundary analysis plot."""
        times = [b["time_position"] for b in boundaries]
        jumps = [b["sample_jump"] for b in boundaries]
        problematic = [b["is_problematic"] for b in boundaries]
        
        fig = go.Figure()
        
        # Normal boundaries
        normal_times = [t for t, p in zip(times, problematic) if not p]
        normal_jumps = [j for j, p in zip(jumps, problematic) if not p]
        
        if normal_times:
            fig.add_trace(go.Scatter(
                x=normal_times, y=normal_jumps,
                mode='markers',
                marker=dict(color='green', size=6),
                name='Normal boundaries'
            ))
        
        # Problematic boundaries
        problem_times = [t for t, p in zip(times, problematic) if p]
        problem_jumps = [j for j, p in zip(jumps, problematic) if p]
        
        if problem_times:
            fig.add_trace(go.Scatter(
                x=problem_times, y=problem_jumps,
                mode='markers',
                marker=dict(color='red', size=8, symbol='x'),
                name='Problematic boundaries'
            ))
        
        # Add threshold line
        fig.add_hline(y=0.1, line_dash="dash", line_color="orange", 
                     annotation_text="Click threshold (0.1)")
        
        fig.update_layout(
            title="Sample Jumps at Packet Boundaries",
            xaxis_title="Time (seconds)",
            yaxis_title="Sample Jump Magnitude",
            height=300
        )
        
        st.plotly_chart(fig, use_container_width=True)
    
    @staticmethod
    def render_click_analysis(click_data: Dict[str, Any]):
        """Renders click and pop analysis."""
        st.subheader("👂 Click & Pop Detection")
        
        if "error" in click_data:
            st.warning(click_data["error"])
            return
        
        total_clicks = click_data.get("total_clicks", 0)
        click_rate = click_data.get("click_rate_per_second", 0)
        duration_ms = click_data.get("analysis_duration_ms", 0)
        
        col1, col2, col3 = st.columns(3)
        col1.metric("Total Clicks", total_clicks)
        col2.metric("Click Rate", f"{click_rate:.2f}/sec")
        col3.metric("Duration", f"{duration_ms:.1f}ms")
        
        clicks = click_data.get("clicks", [])
        
        if clicks:
            st.error(f"🚨 {total_clicks} clicks/pops detected!")
            
            # Plot clicks over time
            AudioAnomalyView.render_click_timeline(clicks, duration_ms)
            
            with st.expander("Show Click Details"):
                for i, click in enumerate(clicks[:20]):  # Show first 20
                    st.write(f"**Click {i+1}:** at {click.get('time_ms', 0):.2f}ms")
                    st.write(f"• Sample index: {click.get('sample_index', 0)}")
                    st.write(f"• Amplitude: {click.get('amplitude', 0):.4f}")
                    st.write(f"• Detection methods: {', '.join(click.get('detection_methods', []))}")
                    if i < len(clicks) - 1:
                        st.divider()
                
                if len(clicks) > 20:
                    st.info(f"... and {len(clicks) - 20} more clicks")
        else:
            st.success("✅ No clicks or pops detected")
    
    @staticmethod
    def render_click_timeline(clicks: List[Dict], duration_ms: float):
        """Renders timeline of detected clicks."""
        times = [c["time_ms"] for c in clicks]
        amplitudes = [abs(c["amplitude"]) for c in clicks]
        
        fig = go.Figure()
        
        fig.add_trace(go.Scatter(
            x=times, y=amplitudes,
            mode='markers',
            marker=dict(
                size=8,
                color=amplitudes,
                colorscale='Reds',
                showscale=True,
                colorbar=dict(title="Amplitude")
            ),
            text=[f"Click at {t:.2f}ms<br>Amplitude: {a:.4f}" for t, a in zip(times, amplitudes)],
            hovertemplate="%{text}<extra></extra>",
            name='Clicks'
        ))
        
        fig.update_layout(
            title="Click/Pop Timeline",
            xaxis_title="Time (ms)",
            yaxis_title="Click Amplitude",
            height=300,
            xaxis=dict(range=[0, duration_ms])
        )
        
        st.plotly_chart(fig, use_container_width=True)
    
    @staticmethod
    def render_analysis_controls():
        """Renders controls for anomaly analysis."""
        st.subheader("🎛️ Analysis Controls")
        
        col1, col2, col3 = st.columns(3)
        
        with col1:
            trim_edges = st.checkbox("Trim Edge Packets", value=True, 
                                   help="Remove first/last 2 packets to avoid capture artifacts")
        
        with col2:
            click_threshold = st.slider("Click Detection Threshold", 0.01, 0.5, 0.1, 0.01,
                                      help="Sensitivity for click/pop detection")
        
        with col3:
            fft_size = st.selectbox("FFT Size", [1024, 2048, 4096, 8192], index=3,
                                   help="FFT size for spectral analysis")
        
        return {
            "trim_edges": trim_edges,
            "click_threshold": click_threshold,
            "fft_size": fft_size
        }