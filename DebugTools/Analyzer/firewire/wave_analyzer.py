# firewire/wave_analyzer.py
import numpy as np
import pandas as pd
from scipy.io import wavfile
from scipy.signal import stft, find_peaks
from typing import Optional, List, Dict, Any, Tuple
import io


class WaveAnalyzer:
    """
    Audio forensic analysis tool for WAV files.
    Detects transients, dropouts, clusters events, and generates spectrograms.
    """
    
    def __init__(self):
        self.sr: Optional[int] = None
        self.data: Optional[np.ndarray] = None
        self.num_channels: int = 0
        self.max_val: int = 0
        self.events: List[Dict[str, Any]] = []
        self.clusters: List[Dict[str, Any]] = []
        
        # Configuration parameters
        self.config = {
            'slice_seconds': None,  # Limit analysis to first N seconds (None = full file)
            'silence_threshold': 0.01,  # Fraction of max amplitude considered silence
            'min_silence_duration_sec': 0.002,  # ≥2 ms = dropout
            'anomaly_std_multiplier': 6,  # Nσ threshold for transients
            'cluster_window_sec': 0.01,  # 10 ms cluster merge window
            'nperseg': 8192,  # STFT window length (power-of-two, high → fewer columns)
            'noverlap': None  # Will be set to nperseg // 2
        }
        self.config['noverlap'] = self.config['nperseg'] // 2
    
    def load_file(self, file_path: str) -> bool:
        """
        Load a WAV file for analysis.
        Returns True if successful, False otherwise.
        """
        try:
            self.sr, self.data = wavfile.read(file_path)
            
            # Slice if requested
            if self.config['slice_seconds'] is not None:
                self.data = self.data[:int(self.sr * self.config['slice_seconds'])]
            
            # Ensure 2-D format (samples, channels)
            if self.data.ndim == 1:
                self.data = self.data[:, None]  # mono → 2-D
                
            self.num_channels = self.data.shape[1]
            self.max_val = np.iinfo(self.data.dtype).max
            
            return True
            
        except Exception as e:
            print(f"Error loading file: {str(e)}")
            return False
    
    def load_from_bytes(self, file_bytes: bytes) -> bool:
        """
        Load a WAV file from bytes (for Streamlit uploads).
        Returns True if successful, False otherwise.
        """
        try:
            # Create a BytesIO object from the bytes
            file_io = io.BytesIO(file_bytes)
            self.sr, self.data = wavfile.read(file_io)
            
            # Slice if requested
            if self.config['slice_seconds'] is not None:
                self.data = self.data[:int(self.sr * self.config['slice_seconds'])]
            
            # Ensure 2-D format (samples, channels)
            if self.data.ndim == 1:
                self.data = self.data[:, None]  # mono → 2-D
                
            self.num_channels = self.data.shape[1]
            self.max_val = np.iinfo(self.data.dtype).max
            
            return True
            
        except Exception as e:
            print(f"Error loading file from bytes: {str(e)}")
            return False
    
    def analyze_events(self) -> Dict[str, Any]:
        """
        Detect transients and dropouts in the audio data.
        Returns analysis results.
        """
        if self.data is None:
            return {"error": "No audio data loaded"}
        
        self.events = []
        
        for ch in range(self.num_channels):
            ch_lbl = 'L' if ch == 0 else 'R' if ch == 1 else f'Ch{ch+1}'
            channel = self.data[:, ch].astype(np.int64)
            
            # Transient detection
            diff = np.abs(np.diff(channel))
            thresh = diff.mean() + self.config['anomaly_std_multiplier'] * diff.std()
            peaks, props = find_peaks(diff, height=thresh)
            
            for idx, height in zip(peaks, props['peak_heights']):
                likelihood = min(1.0, (height - diff.mean()) / (thresh - diff.mean()))
                self.events.append({
                    'channel': ch_lbl,
                    'time_sec': idx / self.sr,
                    'type': 'transient',
                    'value': int(height),
                    'likelihood': float(likelihood)
                })
            
            # Dropout detection
            abs_norm = np.abs(channel) / self.max_val
            silent = abs_norm < self.config['silence_threshold']
            run_start = None
            
            for i, val in enumerate(silent):
                if val and run_start is None:
                    run_start = i
                elif not val and run_start is not None:
                    dur = (i - run_start) / self.sr
                    if dur >= self.config['min_silence_duration_sec']:
                        self.events.append({
                            'channel': ch_lbl,
                            'time_sec': run_start / self.sr,
                            'type': 'dropout',
                            'value': dur,
                            'likelihood': 1.0
                        })
                    run_start = None
            
            # Handle end-of-file dropout
            if run_start is not None:
                dur = (len(channel) - run_start) / self.sr
                if dur >= self.config['min_silence_duration_sec']:
                    self.events.append({
                        'channel': ch_lbl,
                        'time_sec': run_start / self.sr,
                        'type': 'dropout',
                        'value': dur,
                        'likelihood': 1.0
                    })
        
        return {
            "events": self.events,
            "total_events": len(self.events),
            "transients": len([e for e in self.events if e['type'] == 'transient']),
            "dropouts": len([e for e in self.events if e['type'] == 'dropout'])
        }
    
    def cluster_transients(self) -> Dict[str, Any]:
        """
        Cluster nearby transients within the cluster window.
        Returns clustering results.
        """
        if not self.events:
            return {"error": "No events to cluster. Run analyze_events() first."}
        
        self.clusters = []
        df_events = pd.DataFrame(self.events)
        
        for ch_lbl in df_events['channel'].unique():
            ts = df_events[(df_events.channel == ch_lbl) & (df_events.type == 'transient')]
            if ts.empty:
                continue
                
            times = sorted(ts.time_sec.values)
            grp = [times[0]]
            
            for t in times[1:]:
                if t - grp[-1] <= self.config['cluster_window_sec']:
                    grp.append(t)
                else:
                    self.clusters.append({
                        'channel': ch_lbl,
                        'start': grp[0],
                        'end': grp[-1],
                        'count': len(grp)
                    })
                    grp = [t]
            
            if grp:  # Don't forget the last group
                self.clusters.append({
                    'channel': ch_lbl,
                    'start': grp[0],
                    'end': grp[-1],
                    'count': len(grp)
                })
        
        return {
            "clusters": self.clusters,
            "total_clusters": len(self.clusters)
        }
    
    def generate_spectrogram(self, channel: int = 0) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
        """
        Generate spectrogram data for a specific channel.
        Returns (frequencies, times, spectrogram_magnitude).
        """
        if self.data is None:
            raise ValueError("No audio data loaded")
        
        if channel >= self.num_channels:
            raise ValueError(f"Channel {channel} not available. File has {self.num_channels} channels.")
        
        f, t_spec, Z = stft(
            self.data[:, channel], 
            self.sr, 
            nperseg=self.config['nperseg'], 
            noverlap=self.config['noverlap']
        )
        
        return f, t_spec, np.abs(Z)
    
    def get_audio_metrics(self) -> Dict[str, Any]:
        """
        Calculate basic audio metrics for the loaded file.
        """
        if self.data is None:
            return {"error": "No audio data loaded"}
        
        duration_sec = len(self.data) / self.sr
        
        metrics = {
            "sample_rate": self.sr,
            "channels": self.num_channels,
            "duration_seconds": duration_sec,
            "total_samples": len(self.data),
            "bit_depth": self.data.dtype.name,
            "max_possible_value": self.max_val
        }
        
        # Per-channel metrics
        channel_metrics = []
        for ch in range(self.num_channels):
            ch_lbl = 'L' if ch == 0 else 'R' if ch == 1 else f'Ch{ch+1}'
            channel = self.data[:, ch]
            
            # Convert to float for calculations
            channel_float = channel.astype(np.float64) / self.max_val
            
            peak_level = np.max(np.abs(channel_float))
            rms_level = np.sqrt(np.mean(channel_float ** 2))
            dc_offset = np.mean(channel_float)
            
            peak_dbfs = 20 * np.log10(peak_level) if peak_level > 0 else -np.inf
            rms_dbfs = 20 * np.log10(rms_level) if rms_level > 0 else -np.inf
            crest_factor = peak_level / rms_level if rms_level > 0 else np.inf
            
            channel_metrics.append({
                "channel": ch_lbl,
                "peak_level": peak_level,
                "peak_dbfs": peak_dbfs,
                "rms_level": rms_level,
                "rms_dbfs": rms_dbfs,
                "crest_factor": crest_factor,
                "dc_offset": dc_offset
            })
        
        metrics["channel_metrics"] = channel_metrics
        return metrics
    
    def get_events_dataframe(self) -> pd.DataFrame:
        """Return events as a pandas DataFrame."""
        return pd.DataFrame(self.events)
    
    def get_clusters_dataframe(self) -> pd.DataFrame:
        """Return clusters as a pandas DataFrame."""
        return pd.DataFrame(self.clusters)
    
    def update_config(self, **kwargs):
        """Update analysis configuration parameters."""
        for key, value in kwargs.items():
            if key in self.config:
                self.config[key] = value
                if key == 'nperseg':
                    self.config['noverlap'] = value // 2