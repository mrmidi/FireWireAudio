# mvc/packet_analysis_views.py
import streamlit as st
import pandas as pd
import plotly.graph_objects as go
import plotly.express as px
import json
from typing import List, Dict, Any
from firewire.cip_packet import CIPPacket

class PacketAnalysisView:
    """View component for enhanced packet analysis tab."""
    
    @staticmethod
    def render_packet_health_overview(analysis_results: Dict[str, Any]):
        """Renders overall packet health status."""
        st.header("📦 Packet Stream Health Analysis")
        
        if "error" in analysis_results:
            st.error(f"Analysis failed: {analysis_results['error']}")
            return
        
        # Health score display
        col1, col2, col3, col4 = st.columns(4)
        
        health_score = analysis_results.get("packet_health_score", 0)
        severity = analysis_results.get("severity_level", "unknown")
        
        # Color-code the health score
        if health_score >= 95:
            col1.success(f"**Health Score: {health_score}%**")
        elif health_score >= 85:
            col1.info(f"**Health Score: {health_score}%**")
        elif health_score >= 70:
            col1.warning(f"**Health Score: {health_score}%**")
        else:
            col1.error(f"**Health Score: {health_score}%**")
        
        col2.metric("Severity Level", severity.upper())
        col3.metric("Total Packets", analysis_results.get("total_packets_analyzed", 0))
        col4.metric("Total Issues", analysis_results.get("total_issues", 0))
        
        # Issue summary
        issue_summary = analysis_results.get("issue_summary", {})
        if any(issue_summary.values()):
            st.subheader("🚨 Issue Summary")
            
            issue_cols = st.columns(4)
            issue_cols[0].metric("DBC Issues", issue_summary.get("dbc_discontinuities", 0))
            issue_cols[1].metric("Length Errors", issue_summary.get("length_errors", 0))
            issue_cols[2].metric("Dropouts", issue_summary.get("dropout_packets", 0))
            issue_cols[3].metric("Pattern Anomalies", issue_summary.get("pattern_anomalies", 0))
        
        # Recommendations
        recommendations = analysis_results.get("recommendations", [])
        if recommendations:
            st.subheader("💡 Recommendations")
            for i, rec in enumerate(recommendations, 1):
                st.write(f"{i}. {rec}")
    
    @staticmethod
    def render_dbc_analysis_enhanced(dbc_results: Dict[str, Any]):
        """Renders enhanced DBC continuity analysis."""
        st.subheader("🔄 Data Block Counter (DBC) Analysis")
        
        discontinuities = dbc_results.get("discontinuities", [])
        
        if not discontinuities:
            st.success("✅ No DBC discontinuities detected - Perfect synchronization!")
        else:
            st.error(f"⚠️ Found {len(discontinuities)} DBC discontinuities")
            
            # Show detailed discontinuity information
            if discontinuities:
                df = pd.DataFrame(discontinuities)
                
                # Group by channel for better display
                for channel in df['channel'].unique():
                    channel_issues = df[df['channel'] == channel]
                    
                    with st.expander(f"📡 Channel {channel} Issues ({len(channel_issues)} found)"):
                        for _, issue in channel_issues.iterrows():
                            issue_type = "After No-Data" if issue.get('after_no_data', False) else "Data-to-Data"
                            st.write(f"**Packet {issue['packet_index']}** ({issue['packet_type']}, {issue_type})")
                            st.write(f"Expected DBC: `0x{issue['expected_dbc']:02X}`, Got: `0x{issue['actual_dbc']:02X}`")
                            st.write(f"Description: {issue['description']}")
                            st.divider()
    
    @staticmethod
    def render_length_errors(length_results: Dict[str, Any]):
        """Renders length error analysis."""
        st.subheader("📏 Packet Length Errors")
        
        total_errors = length_results.get("total_errors", 0)
        
        if total_errors == 0:
            st.success("✅ No packet length errors detected")
        else:
            st.error(f"⚠️ Found {total_errors} length errors")
            
            high_severity = length_results.get("high_severity", 0)
            moderate_severity = length_results.get("moderate_severity", 0)
            low_severity = length_results.get("low_severity", 0)
            explicit_errors = length_results.get("explicit_length_errors", 0)
            
            col1, col2, col3, col4 = st.columns(4)
            col1.error(f"High Severity: {high_severity}")
            col2.warning(f"Moderate Severity: {moderate_severity}")
            col3.info(f"Low Severity: {low_severity}")
            col4.metric("Explicit LENGTH ERRORs", explicit_errors)
            
            errors = length_results.get("errors", [])
            if errors:
                st.subheader("📋 Length Error Details")
                
                # Create a dataframe for better display
                df = pd.DataFrame(errors)
                display_columns = ['packet_index', 'channel', 'timestamp', 'declared_size', 
                                 'actual_size', 'size_diff', 'severity', 'has_explicit_length_error']
                st.dataframe(
                    df[display_columns],
                    use_container_width=True,
                    column_config={
                        "has_explicit_length_error": st.column_config.CheckboxColumn("LENGTH ERROR Flag")
                    }
                )
                
                # Show sample hex data for severe errors
                severe_errors = [e for e in errors if e["severity"] == "high"]
                if severe_errors:
                    with st.expander("🔍 Sample Severe Error Hex Data"):
                        for i, error in enumerate(severe_errors[:3]):  # Show first 3
                            st.write(f"**Error {i+1}** (Packet {error['packet_index']}):")
                            if error.get('has_explicit_length_error'):
                                st.warning(f"🚨 Explicit LENGTH ERROR detected - {error.get('length_error_bytes', 0)} bytes snooped")
                            st.code(error.get('hex_data_truncated', 'No hex data'), language='text')
    
    @staticmethod
    def render_dropout_analysis(dropout_results: Dict[str, Any]):
        """Renders audio dropout analysis."""
        st.subheader("🔇 Audio Dropout Analysis")
        
        total_dropouts = dropout_results.get("total_dropout_packets", 0)
        dropout_regions = dropout_results.get("dropout_regions", 0)
        
        if total_dropouts == 0:
            st.success("✅ No audio dropouts detected")
        else:
            st.warning(f"⚠️ Found {total_dropouts} dropout packets in {dropout_regions} regions")
            
            # Dropdown type breakdown
            dropouts = dropout_results.get("dropouts", [])
            if dropouts:
                dropout_types = {}
                for dropout in dropouts:
                    dtype = dropout.get("type", "unknown")
                    dropout_types[dtype] = dropout_types.get(dtype, 0) + 1
                
                col1, col2, col3 = st.columns(3)
                col1.metric("All Zeros", dropout_types.get("all_zeros", 0))
                col2.metric("Near Silence", dropout_types.get("near_silence", 0))
                col3.metric("Repeated Pattern", dropout_types.get("repeated_pattern", 0))
                
                # Show dropout regions
                regions = dropout_results.get("regions", [])
                if regions:
                    st.subheader("📊 Dropout Regions")
                    regions_df = pd.DataFrame(regions)
                    st.dataframe(regions_df, use_container_width=True)
                
                # Show individual dropouts with details
                with st.expander("🔍 Individual Dropout Details"):
                    for i, dropout in enumerate(dropouts[:10]):  # Show first 10
                        st.write(f"**Dropout {i+1}** - Type: {dropout['type']}")
                        st.write(f"Channel: {dropout['channel']}, Packet: {dropout['packet_index']}")
                        st.write(f"Timestamp: {dropout['timestamp']}, DBC: 0x{dropout['dbc']:02X}")
                        st.write(f"Max Amplitude: {dropout['max_amplitude']:.6f}")
                        if dropout.get('hex_samples'):
                            st.code(' '.join(dropout['hex_samples'][:3]), language='text')
                        st.divider()
    
    @staticmethod
    def render_pattern_analysis(pattern_results: Dict[str, Any]):
        """Renders pattern anomaly analysis."""
        st.subheader("🔁 Pattern Anomaly Analysis")
        
        total_anomalies = pattern_results.get("total_pattern_anomalies", 0)
        
        if total_anomalies == 0:
            st.success("✅ No suspicious patterns detected")
        else:
            st.warning(f"⚠️ Found {total_anomalies} pattern anomalies")
            
            anomalies = pattern_results.get("anomalies", [])
            if anomalies:
                # Group by pattern type
                pattern_types = {}
                for anomaly in anomalies:
                    ptype = anomaly.get("type", "unknown")
                    pattern_types[ptype] = pattern_types.get(ptype, 0) + 1
                
                for ptype, count in pattern_types.items():
                    st.metric(f"{ptype.replace('_', ' ').title()}", count)
                
                # Show detailed anomalies
                with st.expander("🔍 Pattern Anomaly Details"):
                    for i, anomaly in enumerate(anomalies):
                        st.write(f"**Anomaly {i+1}** - Type: {anomaly['type']}")
                        st.write(f"Channel: {anomaly['channel']}, Packet: {anomaly['packet_index']}")
                        st.write(f"Timestamp: {anomaly['timestamp']}")
                        
                        if anomaly['type'] == "single_value_repetition":
                            st.write(f"Repeated Value: {anomaly['value']:.6f}")
                        elif anomaly['type'] == "alternating_pattern":
                            st.write(f"Pattern Values: {anomaly['pattern_values']}")
                        
                        st.write(f"Sample Count: {anomaly['sample_count']}")
                        st.divider()
    
    @staticmethod
    def render_export_controls(analysis_results: Dict[str, Any], packet_samples: Dict[str, Any]):
        """Renders export controls and options."""
        st.subheader("📤 Export Analysis Report")
        
        col1, col2 = st.columns(2)
        
        with col1:
            # Export full analysis as JSON
            if st.button("📄 Export Full Analysis (JSON)", use_container_width=True):
                export_data = {
                    "analysis_results": analysis_results,
                    "packet_samples": packet_samples,
                    "export_timestamp": analysis_results.get("analysis_timestamp", "unknown")
                }
                
                json_str = json.dumps(export_data, indent=2, default=str)
                st.download_button(
                    label="💾 Download Analysis Report",
                    data=json_str,
                    file_name=f"firewire_packet_analysis_{analysis_results.get('analysis_timestamp', 'unknown')[:10]}.json",
                    mime="application/json",
                    use_container_width=True
                )
        
        with col2:
            # Export packet samples only
            if st.button("📦 Export Packet Samples (JSON)", use_container_width=True):
                json_str = json.dumps(packet_samples, indent=2, default=str)
                st.download_button(
                    label="💾 Download Packet Samples",
                    data=json_str,
                    file_name=f"firewire_packet_samples_{analysis_results.get('analysis_timestamp', 'unknown')[:10]}.json",
                    mime="application/json",
                    use_container_width=True
                )
        
        # Preview export data
        with st.expander("👁️ Preview Export Data"):
            tab1, tab2 = st.tabs(["Analysis Summary", "Packet Samples"])
            
            with tab1:
                st.json({
                    "health_score": analysis_results.get("packet_health_score"),
                    "severity": analysis_results.get("severity_level"),
                    "total_issues": analysis_results.get("total_issues"),
                    "issue_summary": analysis_results.get("issue_summary", {}),
                    "recommendations": analysis_results.get("recommendations", [])
                })
            
            with tab2:
                # Show sample counts
                for sample_type, samples in packet_samples.items():
                    st.write(f"**{sample_type.replace('_', ' ').title()}:** {len(samples)} samples")
                
                # Show a sample packet if available
                if packet_samples.get("length_error_samples"):
                    st.write("**Sample Length Error Packet:**")
                    st.json(packet_samples["length_error_samples"][0])
    
    @staticmethod
    def render_packet_timeline(analysis_results: Dict[str, Any]):
        """Renders a timeline view of packet issues."""
        st.subheader("📈 Issue Timeline")
        
        # Collect all issues with timestamps
        timeline_events = []
        
        # DBC issues
        dbc_issues = analysis_results.get("dbc_analysis", {}).get("discontinuities", [])
        for issue in dbc_issues:
            timeline_events.append({
                "packet_index": issue.get("packet_index", 0),
                "type": "DBC Discontinuity",
                "channel": issue.get("channel", 0),
                "severity": "high" if issue.get("packet_type") == "data" else "moderate",
                "description": issue.get("description", "")
            })
        
        # Length errors
        length_issues = analysis_results.get("length_errors", {}).get("errors", [])
        for issue in length_issues:
            timeline_events.append({
                "packet_index": issue.get("packet_index", 0),
                "type": "Length Error",
                "channel": issue.get("channel", 0),
                "severity": issue.get("severity", "moderate"),
                "description": f"Size diff: {issue.get('size_diff', 0)}"
            })
        
        # Dropouts
        dropout_issues = analysis_results.get("dropout_analysis", {}).get("dropouts", [])
        for issue in dropout_issues:
            timeline_events.append({
                "packet_index": issue.get("packet_index", 0),
                "type": "Audio Dropout",
                "channel": issue.get("channel", 0),
                "severity": "moderate",
                "description": f"Type: {issue.get('type', 'unknown')}"
            })
        
        if timeline_events:
            # Sort by packet index
            timeline_events.sort(key=lambda x: x["packet_index"])
            
            # Create timeline plot
            df = pd.DataFrame(timeline_events)
            
            # Color mapping for issue types
            color_map = {
                "DBC Discontinuity": "red",
                "Length Error": "orange", 
                "Audio Dropout": "blue"
            }
            
            fig = px.scatter(
                df, 
                x="packet_index", 
                y="type",
                color="type",
                color_discrete_map=color_map,
                hover_data=["channel", "severity", "description"],
                title="Packet Issues Timeline"
            )
            
            fig.update_layout(
                xaxis_title="Packet Index",
                yaxis_title="Issue Type",
                height=300
            )
            
            st.plotly_chart(fig, use_container_width=True)
        else:
            st.info("No issues to display in timeline")
    
    @staticmethod
    def render_channel_comparison(analysis_results: Dict[str, Any]):
        """Renders comparison of issues across channels."""
        st.subheader("📡 Channel Comparison")
        
        # Collect issues by channel
        channel_stats = {}
        
        # DBC issues by channel
        dbc_issues = analysis_results.get("dbc_analysis", {}).get("discontinuities", [])
        for issue in dbc_issues:
            channel = issue.get("channel", 0)
            if channel not in channel_stats:
                channel_stats[channel] = {"dbc_issues": 0, "length_errors": 0, "dropouts": 0}
            channel_stats[channel]["dbc_issues"] += 1
        
        # Length errors by channel
        length_issues = analysis_results.get("length_errors", {}).get("errors", [])
        for issue in length_issues:
            channel = issue.get("channel", 0)
            if channel not in channel_stats:
                channel_stats[channel] = {"dbc_issues": 0, "length_errors": 0, "dropouts": 0}
            channel_stats[channel]["length_errors"] += 1
        
        # Dropouts by channel
        dropout_issues = analysis_results.get("dropout_analysis", {}).get("dropouts", [])
        for issue in dropout_issues:
            channel = issue.get("channel", 0)
            if channel not in channel_stats:
                channel_stats[channel] = {"dbc_issues": 0, "length_errors": 0, "dropouts": 0}
            channel_stats[channel]["dropouts"] += 1
        
        if channel_stats:
            # Display as metrics
            channels = sorted(channel_stats.keys())
            cols = st.columns(len(channels))
            
            for i, channel in enumerate(channels):
                stats = channel_stats[channel]
                total_issues = sum(stats.values())
                
                with cols[i]:
                    st.metric(f"Channel {channel}", f"{total_issues} issues")
                    st.write(f"DBC: {stats['dbc_issues']}")
                    st.write(f"Length: {stats['length_errors']}")
                    st.write(f"Dropouts: {stats['dropouts']}")
        else:
            st.info("No channel-specific issues detected")
