# Audio Forensic Analysis Tool
# -----------------------------------------------
# Detects transients, dropouts, clusters events, and plots per-channel spectrograms
# Memory-safe: slices long audio and uses large nperseg to reduce STFT bins.

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from scipy.io import wavfile
from scipy.signal import stft, find_peaks
import ace_tools as tools

# ----------------------- CONFIG -----------------------
PATH = '/mnt/data/DUET_GLITCH.wav'  # WAV to analyse
SLICE_SECONDS = 30                 # Limit analysis to first N seconds (None = full file)
SILENCE_THRESHOLD = 0.01           # Fraction of max amplitude considered silence
MIN_SILENCE_DURATION_SEC = 0.002   # ≥2 ms = dropout
ANOMALY_STD_MULTIPLIER = 6         # Nσ threshold for transients
CLUSTER_WINDOW_SEC = 0.01          # 10 ms cluster merge window
NPERSEG = 8192                     # STFT window length (power-of-two, high → fewer columns)
NOVERLAP = NPERSEG // 2            # 50 % overlap


# ----------------------- LOAD -------------------------
print(f"Loading {PATH} …")
sr, data = wavfile.read(PATH)
if SLICE_SECONDS is not None:
    data = data[: int(sr * SLICE_SECONDS)]

if data.ndim == 1:
    data = data[:, None]  # mono → 2-D (samples, channels)
num_channels = data.shape[1]
max_val = np.iinfo(data.dtype).max

# ------------------ EVENT DETECTION -------------------
events = []
for ch in range(num_channels):
    ch_lbl = 'L' if ch == 0 else 'R'
    channel = data[:, ch].astype(np.int64)

    # Transient detection
    diff = np.abs(np.diff(channel))
    thresh = diff.mean() + ANOMALY_STD_MULTIPLIER * diff.std()
    peaks, props = find_peaks(diff, height=thresh)
    for idx, height in zip(peaks, props['peak_heights']):
        likelihood = min(1.0, (height - diff.mean()) / (thresh - diff.mean()))
        events.append({
            'channel': ch_lbl,
            'time_sec': idx / sr,
            'type': 'transient',
            'value': int(height),
            'likelihood': float(likelihood)
        })

    # Dropout detection
    abs_norm = np.abs(channel) / max_val
    silent = abs_norm < SILENCE_THRESHOLD
    run_start = None
    for i, val in enumerate(silent):
        if val and run_start is None:
            run_start = i
        elif not val and run_start is not None:
            dur = (i - run_start) / sr
            if dur >= MIN_SILENCE_DURATION_SEC:
                events.append({
                    'channel': ch_lbl,
                    'time_sec': run_start / sr,
                    'type': 'dropout',
                    'value': dur,
                    'likelihood': 1.0
                })
            run_start = None
    if run_start is not None:
        dur = (len(channel) - run_start) / sr
        if dur >= MIN_SILENCE_DURATION_SEC:
            events.append({
                'channel': ch_lbl,
                'time_sec': run_start / sr,
                'type': 'dropout',
                'value': dur,
                'likelihood': 1.0
            })

# Convert to DataFrame
DfEvents = pd.DataFrame(events)

# --------------------- CLUSTERING ---------------------
clusters = []
for ch_lbl in DfEvents['channel'].unique():
    ts = DfEvents[(DfEvents.channel == ch_lbl) & (DfEvents.type == 'transient')]
    if ts.empty:
        continue
    times = sorted(ts.time_sec.values)
    grp = [times[0]]
    for t in times[1:]:
        if t - grp[-1] <= CLUSTER_WINDOW_SEC:
            grp.append(t)
        else:
            clusters.append({'channel': ch_lbl, 'start': grp[0], 'end': grp[-1], 'count': len(grp)})
            grp = [t]
    clusters.append({'channel': ch_lbl, 'start': grp[0], 'end': grp[-1], 'count': len(grp)})
DfClusters = pd.DataFrame(clusters)

# Display results
tools.display_dataframe_to_user('Audio Anomalies', DfEvents)
if not DfClusters.empty:
    tools.display_dataframe_to_user('Clustered Transients', DfClusters)

# ------------------- SPECTROGRAMS ---------------------
for ch in range(num_channels):
    ch_lbl = 'L' if ch == 0 else 'R'
    f, t_spec, Z = stft(data[:, ch], sr, nperseg=NPERSEG, noverlap=NOVERLAP)
    plt.figure(figsize=(10, 4))
    plt.pcolormesh(t_spec, f, np.abs(Z), shading='gouraud')
    plt.title(f'Spectrogram – {ch_lbl} channel')
    plt.ylabel('Hz')
    plt.xlabel('s')
    plt.tight_layout()
    plt.show()