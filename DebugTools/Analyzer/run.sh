#!/bin/bash

# FireWire Audio Analyzer Startup Script
# This script activates the virtual environment and starts the Streamlit app

echo "🎵 Starting FireWire Audio Packet Analyzer..."
echo "📦 Activating virtual environment..."

# Change to the script directory
cd "$(dirname "$0")"

# Check if virtual environment exists
if [ ! -d ".venv" ]; then
    echo "❌ Virtual environment not found!"
    echo "Please run: python3 -m venv .venv && source .venv/bin/activate && pip install -r requirements.txt"
    exit 1
fi

# Activate virtual environment and start Streamlit
source .venv/bin/activate
echo "🚀 Starting Streamlit application..."
echo "📍 Open your browser to: http://localhost:8501"
echo "🛑 Press Ctrl+C to stop the application"
echo ""

streamlit run app.py
