#!/usr/bin/env bash
#
# Usage: fwdebug.sh driver|daemon
#

TARGET_TYPE="$1"
LLDB_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" # Get directory of this script

case "$TARGET_TYPE" in
  driver)
    PROCESS_NAME="com.apple.audio.Core-Audio-Driver-Service.helper"
    LLDB_COMMAND_FILE="${LLDB_SCRIPT_DIR}/fwdbg_driver.lldb"
    ;;
  daemon)
    PROCESS_NAME="FWADaemon"
    LLDB_COMMAND_FILE="${LLDB_SCRIPT_DIR}/fwdbg_daemon.lldb"
    ;;
  *)
    echo "Usage: $0 driver|daemon" >&2
    exit 1
    ;;
esac

PID=$(pgrep -x "$PROCESS_NAME")

if [[ -z "$PID" ]]; then
  echo "Couldn't find PID for $TARGET_TYPE ($PROCESS_NAME)" >&2
  if [[ "$TARGET_TYPE" == "daemon" ]]; then
    echo "Try launching the daemon first."
  fi
  exit 2
fi

if [[ ! -f "$LLDB_COMMAND_FILE" ]]; then
  echo "Error: LLDB command file not found: $LLDB_COMMAND_FILE" >&2
  exit 3
fi

# Check if Python script exists
PYTHON_SCRIPT="${LLDB_SCRIPT_DIR}/fwdbg_daemon_bp.py"
if [[ ! -f "$PYTHON_SCRIPT" ]]; then
  echo "Error: Python breakpoint script not found: $PYTHON_SCRIPT" >&2
  exit 4
fi

echo "Attaching to $TARGET_TYPE (PID=$PID, Process=$PROCESS_NAME) using $LLDB_COMMAND_FILE"

# Use --source-quietly to suppress command echoing
# Add --batch if you want to run non-interactively
exec sudo lldb -p "$PID" --source "$LLDB_COMMAND_FILE"