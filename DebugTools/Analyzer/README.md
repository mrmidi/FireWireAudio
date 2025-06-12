# FireWire Audio Packet Analyzer

A Streamlit-based web application for analyzing FireWire audio packet logs from Apple FireBug tool.

## Features

- **CIP Packet Analysis**: Stream format detection, DBC continuity checking, SYT timestamp analysis
- **Audio Waveform Analysis**: Peak/RMS levels, frequency spectrum, quality metrics
- **Detailed Packet Log**: Individual packet inspection with CIP header breakdown
- **Data/No-Data Packet Analysis**: Proper handling of valid no-data packets (SYT=0xFFFF)
- **Enhanced DBC Logic**: Correct DBC increment handling for mixed data/no-data streams

## Setup

### 1. Create Virtual Environment
```bash
python3 -m venv .venv
```

### 2. Activate Virtual Environment
```bash
source .venv/bin/activate
```

### 3. Install Requirements
```bash
pip install -r requirements.txt
```

### 4. Run the Application
```bash
streamlit run app.py
```

Or use the provided script:
```bash
./run.sh
```

## Usage

1. Upload one or more FireBug `.txt` or `.log` files
2. Navigate through the three main tabs:
   - **📦 CIP Packet Analysis**: Overview of stream format and packet continuity
   - **🔊 Audio Waveform Analysis**: Audio quality metrics and visualization
   - **🔍 Detailed Packet Log**: Individual packet inspection

## DBC (Data Block Counter) Logic

The analyzer implements proper DBC increment logic:

- **Data packets**: DBC increments by SYT_INTERVAL (8, 16, or 32 based on sample rate)
- **No-data packets**: DBC increments by 8 regardless of sample rate
- **After no-data**: Next data packet DBC should NOT increment from last data packet DBC

## Valid No-Data Packets

A valid no-data packet should have:
- SYT field = 0xFFFF
- Proper SFC (Sampling Frequency Code) in FDF field (not 0xFF)
- SFC should match the stream's sample rate (0=32kHz, 1=44.1kHz, 2=48kHz, etc.)

## Dependencies

See `requirements.txt` for complete list. Main dependencies:
- streamlit
- numpy
- scipy
- plotly
- pandas

## File Structure

```
analyzer/
├── app.py                 # Main Streamlit application
├── firewire/
│   ├── __init__.py
│   ├── analyzer.py        # Core analysis logic
│   ├── cip_packet.py      # CIP packet parser
│   └── log_parser.py      # FireBug log parser
├── requirements.txt       # Python dependencies
├── run.sh                # Startup script
└── README.md             # This file
```
